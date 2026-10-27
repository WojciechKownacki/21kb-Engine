#include "kb/net/Session.hpp"

#include "Wire.hpp"
#include "kb/net/ByteIO.hpp"

#include <algorithm>
#include <utility>

namespace kb::net {

namespace {

constexpr std::uint16_t kMagic = wire::kMagic;
constexpr std::uint8_t kVersion = wire::kVersion;
constexpr std::uint8_t kTypeData = wire::kTypeData;

constexpr std::size_t kHeaderBytes = 12U; // magic, version, type, packet number
constexpr std::size_t kTagBytes = kb::security::kAeadTagBytes;
constexpr std::size_t kAckBytes = 12U;    // latest packet number received, bitmap of the 32 before it
constexpr std::size_t kReliableRecordOverhead = 8U;
constexpr std::size_t kUnreliableRecordOverhead = 6U;

constexpr std::uint8_t kRecordReliable = 1U;
constexpr std::uint8_t kRecordUnreliable = 2U;
constexpr std::uint8_t kRecordDisconnect = 3U;

constexpr std::size_t kMaxRecordsPerPacket = 64U;
constexpr std::size_t kReliableWindow = 256U;    // message ids in flight per channel
constexpr std::size_t kUnreliableQueueLimit = 256U;
constexpr std::size_t kSentHistory = 512U;
constexpr std::uint32_t kDisconnectNotices = 3U;
constexpr std::size_t kMaxFragmentsPerMessage = 255U;

[[nodiscard]] kb::security::AeadNonce MakeNonce(std::uint64_t packetNumber) noexcept {
    kb::security::AeadNonce nonce{};
    for (unsigned index = 0U; index < 8U; ++index) {
        nonce[index] = static_cast<std::uint8_t>((packetNumber >> (8U * index)) & 0xFFU);
    }
    return nonce;
}

// True when `later` comes after `earlier` in 16 bit wrap-around order.
[[nodiscard]] constexpr bool IsNewer(std::uint16_t later, std::uint16_t earlier) noexcept {
    const std::uint16_t difference = static_cast<std::uint16_t>(later - earlier);
    return difference != 0U && difference < 0x8000U;
}

[[nodiscard]] std::size_t DatagramLimit(const SessionConfig& config) noexcept {
    return std::min(kMaxDatagramBytes, config.budget.packetBytes);
}

} // namespace

bool SessionConfig::IsValid() const noexcept {
    if (!kb::network::IsValidNetworkBudget(budget) || !kb::network::IsValidSecurityLimits(limits)) {
        return false;
    }
    if (channels.empty() || channels.size() > 8U || maxInboxMessages == 0U) {
        return false;
    }
    const std::size_t datagram = DatagramLimit(*this);
    if (datagram < kHeaderBytes + kTagBytes + kAckBytes + kReliableRecordOverhead + 64U) {
        return false;
    }
    const std::size_t plaintext = datagram - kHeaderBytes - kTagBytes;
    const std::size_t fragment = plaintext - kAckBytes - kReliableRecordOverhead;
    if (maxMessageBytes == 0U || (maxMessageBytes + fragment - 1U) / fragment > kMaxFragmentsPerMessage ||
        budget.maxQueuedBytes < maxMessageBytes) {
        return false;
    }
    if (maxUnreliableBytes == 0U || maxUnreliableBytes + kUnreliableRecordOverhead + kAckBytes > plaintext) {
        return false;
    }
    return minRtoMilliseconds > 0U && minRtoMilliseconds <= initialRtoMilliseconds &&
        initialRtoMilliseconds <= maxRtoMilliseconds && keepAliveMilliseconds > 0U && timeoutMilliseconds > keepAliveMilliseconds;
}

Session::Session(SessionConfig config, const SessionKey& sendKey, const SessionKey& receiveKey, std::uint64_t nowMilliseconds)
    : config_(std::move(config)), lastReceiveMs_(nowMilliseconds), lastSendMs_(nowMilliseconds) {
    std::copy(sendKey.begin(), sendKey.end(), sendKey_.data());
    std::copy(receiveKey.begin(), receiveKey.end(), receiveKey_.data());
    channels_.resize(config_.channels.size());
    for (std::size_t index = 0U; index < channels_.size(); ++index) {
        channels_[index].mode = config_.channels[index];
    }
}

std::size_t Session::MaxPlaintextBytes() const noexcept {
    return DatagramLimit(config_) - kHeaderBytes - kTagBytes;
}

std::size_t Session::FragmentBytes() const noexcept {
    return MaxPlaintextBytes() - kAckBytes - kReliableRecordOverhead;
}

std::uint64_t Session::RtoFor(std::uint32_t sends) const noexcept {
    std::uint64_t base = config_.initialRtoMilliseconds;
    if (haveRtt_) {
        base = std::max<std::uint64_t>(config_.minRtoMilliseconds,
            static_cast<std::uint64_t>(stats_.smoothedRttMilliseconds) + 4U * static_cast<std::uint64_t>(rttVarianceMs_));
    }
    const std::uint32_t shift = sends > 1U ? std::min<std::uint32_t>(sends - 1U, 4U) : 0U;
    return std::clamp<std::uint64_t>(base << shift, config_.minRtoMilliseconds, config_.maxRtoMilliseconds);
}

SendResult Session::Send(std::uint8_t channelIndex, std::span<const std::uint8_t> data) {
    if (closed_ || disconnectRequested_) {
        return SendResult::Closed;
    }
    if (channelIndex >= channels_.size()) {
        return SendResult::InvalidChannel;
    }
    Channel& channel = channels_[channelIndex];
    if (channel.mode == ChannelMode::UnreliableSequenced) {
        if (data.size() > config_.maxUnreliableBytes) {
            return SendResult::TooLarge;
        }
        if (unreliableOut_.size() >= kUnreliableQueueLimit) {
            return SendResult::Busy;
        }
        unreliableOut_.push_back(Queued{ channelIndex, channel.nextUnreliableSeq++, std::vector<std::uint8_t>(data.begin(), data.end()) });
        return SendResult::Queued;
    }

    if (data.size() > config_.maxMessageBytes) {
        return SendResult::TooLarge;
    }
    if (queuedSendBytes_ + data.size() > config_.budget.maxQueuedBytes || channel.out.size() >= kReliableWindow) {
        return SendResult::Busy;
    }
    const std::size_t fragmentBytes = FragmentBytes();
    OutMessage message;
    message.id = channel.nextSendId++;
    message.bytes = data.size();
    std::size_t offset = 0U;
    do {
        const std::size_t length = std::min(fragmentBytes, data.size() - offset);
        Fragment fragment;
        fragment.data.assign(data.begin() + static_cast<std::ptrdiff_t>(offset), data.begin() + static_cast<std::ptrdiff_t>(offset + length));
        message.fragments.push_back(std::move(fragment));
        offset += length;
    } while (offset < data.size());
    queuedSendBytes_ += message.bytes;
    channel.out.push_back(std::move(message));
    return SendResult::Queued;
}

std::optional<std::vector<std::uint8_t>> Session::BuildPacket(std::uint64_t nowMilliseconds) {
    if (closed_) {
        return std::nullopt;
    }
    if (disconnectRequested_ && disconnectNotices_ >= kDisconnectNotices) {
        closed_ = true;
        return std::nullopt;
    }
    const bool keepAliveDue = nowMilliseconds >= lastSendMs_ + config_.keepAliveMilliseconds;

    std::vector<std::uint8_t> plain;
    plain.reserve(MaxPlaintextBytes());
    ByteWriter writer(plain);
    writer.U64(highestReceived_);
    writer.U32(static_cast<std::uint32_t>(receivedBitmap_ >> 1U));
    const std::size_t limit = MaxPlaintextBytes();
    std::vector<FragmentRef> references;

    if (disconnectRequested_) {
        writer.U8(kRecordDisconnect);
        ++disconnectNotices_;
    } else {
        bool full = false;
        for (std::size_t channelIndex = 0U; channelIndex < channels_.size() && !full; ++channelIndex) {
            Channel& channel = channels_[channelIndex];
            if (channel.mode != ChannelMode::ReliableOrdered) {
                continue;
            }
            for (OutMessage& message : channel.out) {
                for (std::size_t fragmentIndex = 0U; fragmentIndex < message.fragments.size(); ++fragmentIndex) {
                    Fragment& fragment = message.fragments[fragmentIndex];
                    if (fragment.acked) {
                        continue;
                    }
                    const bool due = !fragment.everSent || nowMilliseconds >= fragment.lastSent + RtoFor(fragment.sends);
                    if (!due) {
                        continue;
                    }
                    if (plain.size() + kReliableRecordOverhead + fragment.data.size() > limit) {
                        full = true;
                        break;
                    }
                    writer.U8(kRecordReliable);
                    writer.U8(static_cast<std::uint8_t>(channelIndex));
                    writer.U16(message.id);
                    writer.U8(static_cast<std::uint8_t>(fragmentIndex));
                    writer.U8(static_cast<std::uint8_t>(message.fragments.size()));
                    writer.U16(static_cast<std::uint16_t>(fragment.data.size()));
                    writer.Bytes(fragment.data);
                    references.push_back(FragmentRef{ static_cast<std::uint8_t>(channelIndex), message.id, static_cast<std::uint8_t>(fragmentIndex) });
                    ++stats_.fragmentsSent;
                    if (fragment.everSent) {
                        ++stats_.fragmentsRetransmitted;
                    }
                    fragment.everSent = true;
                    ++fragment.sends;
                    fragment.lastSent = nowMilliseconds;
                }
                if (full) {
                    break;
                }
            }
        }
        while (!unreliableOut_.empty()) {
            const Queued& queued = unreliableOut_.front();
            if (plain.size() + kUnreliableRecordOverhead + queued.data.size() > limit) {
                break;
            }
            writer.U8(kRecordUnreliable);
            writer.U8(queued.channel);
            writer.U16(queued.sequence);
            writer.U16(static_cast<std::uint16_t>(queued.data.size()));
            writer.Bytes(queued.data);
            unreliableOut_.pop_front();
        }
    }

    if (plain.size() == kAckBytes && !ackPending_ && !keepAliveDue) {
        return std::nullopt;
    }

    const std::uint64_t packetNumber = nextPacketNumber_++;
    std::vector<std::uint8_t> datagram;
    datagram.reserve(kHeaderBytes + plain.size() + kTagBytes);
    ByteWriter header(datagram);
    header.U16(kMagic);
    header.U8(kVersion);
    header.U8(kTypeData);
    header.U64(packetNumber);
    kb::security::AeadTag tag{};
    kb::security::AeadEncryptInPlace(plain, tag, sendKey_, MakeNonce(packetNumber), std::span<const std::uint8_t>(datagram.data(), kHeaderBytes));
    header.Bytes(plain);
    header.Bytes(tag);

    sent_[packetNumber] = SentPacket{ nowMilliseconds, std::move(references) };
    while (sent_.size() > kSentHistory) {
        sent_.erase(sent_.begin());
    }
    ackPending_ = false;
    lastSendMs_ = nowMilliseconds;
    ++stats_.packetsSent;
    stats_.bytesSent += datagram.size();
    if (disconnectRequested_ && disconnectNotices_ >= kDisconnectNotices) {
        closed_ = true;
    }
    return datagram;
}

bool Session::IsReplay(std::uint64_t packetNumber) const noexcept {
    if (packetNumber > highestReceived_) {
        return false;
    }
    const std::uint64_t distance = highestReceived_ - packetNumber;
    return distance >= 64U || ((receivedBitmap_ >> distance) & 1U) != 0U;
}

void Session::MarkReceived(std::uint64_t packetNumber) noexcept {
    if (packetNumber > highestReceived_) {
        const std::uint64_t shift = packetNumber - highestReceived_;
        receivedBitmap_ = shift >= 64U ? 0U : receivedBitmap_ << shift;
        receivedBitmap_ |= 1U;
        highestReceived_ = packetNumber;
    } else {
        receivedBitmap_ |= std::uint64_t{ 1U } << (highestReceived_ - packetNumber);
    }
}

PacketResult Session::ProcessPacket(std::uint64_t nowMilliseconds, std::span<const std::uint8_t> datagram) {
    if (closed_) {
        return PacketResult::Closed;
    }
    const auto reject = [this](PacketResult result) {
        switch (result) {
        case PacketResult::Malformed: ++stats_.rejectedMalformed; break;
        case PacketResult::AuthFailed: ++stats_.rejectedAuth; break;
        case PacketResult::Replay: ++stats_.rejectedReplay; break;
        case PacketResult::Busy: ++stats_.rejectedBusy; break;
        default: break;
        }
        return result;
    };

    if (datagram.size() > DatagramLimit(config_) || datagram.size() < kHeaderBytes + kAckBytes + kTagBytes) {
        return reject(PacketResult::Malformed);
    }
    ByteReader headerReader(datagram);
    const std::uint16_t magic = headerReader.U16();
    const std::uint8_t version = headerReader.U8();
    const std::uint8_t type = headerReader.U8();
    const std::uint64_t packetNumber = headerReader.U64();
    if (magic != kMagic || version != kVersion || type != kTypeData || packetNumber == 0U) {
        return reject(PacketResult::Malformed);
    }
    if (IsReplay(packetNumber)) {
        return reject(PacketResult::Replay);
    }

    const std::size_t cipherBytes = datagram.size() - kHeaderBytes - kTagBytes;
    std::vector<std::uint8_t> plain(datagram.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes),
        datagram.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes + cipherBytes));
    kb::security::AeadTag tag{};
    std::copy_n(datagram.end() - static_cast<std::ptrdiff_t>(kTagBytes), kTagBytes, tag.begin());
    if (!kb::security::AeadDecryptInPlace(plain, tag, receiveKey_, MakeNonce(packetNumber), datagram.first(kHeaderBytes))) {
        return reject(PacketResult::AuthFailed);
    }

    ByteReader reader(plain);
    const std::uint64_t ackLatest = reader.U64();
    const std::uint32_t ackBits = reader.U32();
    if (ackLatest >= nextPacketNumber_) {
        return reject(PacketResult::Malformed); // acknowledges a packet that was never sent
    }

    std::vector<ParsedRecord> records;
    bool peerDisconnect = false;
    bool hasReliable = false;
    while (reader.Remaining() != 0U) {
        if (records.size() >= kMaxRecordsPerPacket) {
            return reject(PacketResult::Malformed);
        }
        ParsedRecord record;
        record.type = reader.U8();
        if (record.type == kRecordReliable) {
            record.channel = reader.U8();
            record.id = reader.U16();
            record.fragment = reader.U8();
            record.fragmentCount = reader.U8();
            const std::uint16_t length = reader.U16();
            record.data = reader.Bytes(length);
            if (!reader.Ok() || record.channel >= channels_.size() || channels_[record.channel].mode != ChannelMode::ReliableOrdered ||
                record.fragmentCount == 0U || record.fragment >= record.fragmentCount || record.data.size() > FragmentBytes() ||
                (static_cast<std::size_t>(record.fragmentCount) - 1U) * FragmentBytes() >= config_.maxMessageBytes) {
                return reject(PacketResult::Malformed);
            }
            hasReliable = true;
        } else if (record.type == kRecordUnreliable) {
            record.channel = reader.U8();
            record.id = reader.U16();
            const std::uint16_t length = reader.U16();
            record.data = reader.Bytes(length);
            if (!reader.Ok() || record.channel >= channels_.size() || channels_[record.channel].mode != ChannelMode::UnreliableSequenced ||
                record.data.size() > config_.maxUnreliableBytes) {
                return reject(PacketResult::Malformed);
            }
        } else if (record.type == kRecordDisconnect) {
            peerDisconnect = true;
        } else {
            return reject(PacketResult::Malformed);
        }
        records.push_back(record);
    }

    const PacketResult committed = CommitRecords(records);
    if (committed != PacketResult::Accepted) {
        return reject(committed);
    }
    MarkReceived(packetNumber);
    lastReceiveMs_ = nowMilliseconds;
    ApplyAcks(nowMilliseconds, ackLatest, ackBits);
    ++stats_.packetsAccepted;
    stats_.bytesAccepted += datagram.size();
    if (hasReliable) {
        ackPending_ = true;
    }
    if (peerDisconnect) {
        peerDisconnected_ = true;
        closed_ = true;
    }
    return PacketResult::Accepted;
}

PacketResult Session::CommitRecords(const std::vector<ParsedRecord>& records) {
    // Phase 1: decide whether the whole packet can be taken. A reliable fragment the receiver cannot
    // keep must not be acknowledged, so the packet is refused as a whole and the sender retransmits.
    std::size_t newBytes = 0U;
    for (const ParsedRecord& record : records) {
        if (record.type != kRecordReliable) {
            continue;
        }
        const Channel& channel = channels_[record.channel];
        const std::uint16_t distance = static_cast<std::uint16_t>(record.id - channel.nextDeliverId);
        if (distance >= 0x8000U) {
            continue; // already delivered: a duplicate
        }
        if (distance >= kReliableWindow) {
            return PacketResult::Busy;
        }
        const auto existing = channel.reassembly.find(record.id);
        if (existing != channel.reassembly.end()) {
            if (existing->second.parts.size() != record.fragmentCount) {
                return PacketResult::Malformed;
            }
            if (existing->second.have[record.fragment]) {
                continue;
            }
        }
        newBytes += record.data.size();
    }
    if (bufferedReassemblyBytes_ + newBytes > config_.budget.maxQueuedBytes) {
        return PacketResult::Busy;
    }

    // Phase 2: apply.
    std::vector<bool> touched(channels_.size(), false);
    for (const ParsedRecord& record : records) {
        Channel& channel = channels_[record.channel];
        if (record.type == kRecordReliable) {
            const std::uint16_t distance = static_cast<std::uint16_t>(record.id - channel.nextDeliverId);
            if (distance >= 0x8000U) {
                ++stats_.duplicateFragments;
                continue;
            }
            InMessage& message = channel.reassembly[record.id];
            if (message.parts.empty()) {
                message.parts.resize(record.fragmentCount);
                message.have.assign(record.fragmentCount, false);
            } else if (message.parts.size() != record.fragmentCount) {
                ++stats_.rejectedMalformed; // two fragments of one message disagree on the count
                continue;
            }
            if (message.have[record.fragment]) {
                ++stats_.duplicateFragments;
                continue;
            }
            message.parts[record.fragment].assign(record.data.begin(), record.data.end());
            message.have[record.fragment] = true;
            ++message.received;
            message.bytes += record.data.size();
            bufferedReassemblyBytes_ += record.data.size();
            touched[record.channel] = true;
        } else if (record.type == kRecordUnreliable) {
            if (channel.haveUnreliable && !IsNewer(record.id, channel.lastUnreliableSeq)) {
                ++stats_.staleUnreliable;
                continue;
            }
            channel.haveUnreliable = true;
            channel.lastUnreliableSeq = record.id;
            if (inbox_.size() >= config_.maxInboxMessages) {
                ++stats_.unreliableDropped;
                continue;
            }
            inbox_.push_back(Message{ record.channel, std::vector<std::uint8_t>(record.data.begin(), record.data.end()) });
            ++stats_.messagesDelivered;
        }
    }
    for (std::size_t index = 0U; index < channels_.size(); ++index) {
        if (touched[index]) {
            DeliverReady(channels_[index], static_cast<std::uint8_t>(index));
        }
    }
    return PacketResult::Accepted;
}

void Session::DeliverReady(Channel& channel, std::uint8_t channelIndex) {
    while (inbox_.size() < config_.maxInboxMessages) {
        const auto found = channel.reassembly.find(channel.nextDeliverId);
        if (found == channel.reassembly.end() || found->second.received != found->second.parts.size()) {
            return;
        }
        InMessage& message = found->second;
        bufferedReassemblyBytes_ -= message.bytes;
        if (message.bytes <= config_.maxMessageBytes) {
            Message delivered;
            delivered.channel = channelIndex;
            delivered.data.reserve(message.bytes);
            for (const std::vector<std::uint8_t>& part : message.parts) {
                delivered.data.insert(delivered.data.end(), part.begin(), part.end());
            }
            inbox_.push_back(std::move(delivered));
            ++stats_.messagesDelivered;
        } else {
            ++stats_.rejectedMalformed; // reassembled larger than the limit: dropped, the order moves on
        }
        channel.reassembly.erase(found);
        ++channel.nextDeliverId;
    }
}

bool Session::PopMessage(Message& message) {
    if (inbox_.empty()) {
        return false;
    }
    message = std::move(inbox_.front());
    inbox_.pop_front();
    // Room in the inbox lets messages that were held back be delivered.
    for (std::size_t index = 0U; index < channels_.size(); ++index) {
        if (channels_[index].mode == ChannelMode::ReliableOrdered) {
            DeliverReady(channels_[index], static_cast<std::uint8_t>(index));
        }
    }
    return true;
}

void Session::ApplyAcks(std::uint64_t nowMilliseconds, std::uint64_t latest, std::uint32_t bits) {
    if (latest == 0U) {
        return;
    }
    AckPacket(nowMilliseconds, latest, true);
    for (std::uint32_t index = 0U; index < 32U; ++index) {
        if (((bits >> index) & 1U) != 0U && latest > index + 1U) {
            AckPacket(nowMilliseconds, latest - 1U - index, false);
        }
    }
}

void Session::AckPacket(std::uint64_t nowMilliseconds, std::uint64_t packetNumber, bool sampleRtt) {
    const auto found = sent_.find(packetNumber);
    if (found == sent_.end()) {
        return;
    }
    if (sampleRtt && nowMilliseconds >= found->second.sentAt) {
        const std::uint32_t sample = static_cast<std::uint32_t>(std::min<std::uint64_t>(nowMilliseconds - found->second.sentAt, 60'000U));
        if (!haveRtt_) {
            stats_.smoothedRttMilliseconds = sample;
            rttVarianceMs_ = sample / 2U;
            haveRtt_ = true;
        } else {
            const std::uint32_t smoothed = stats_.smoothedRttMilliseconds;
            const std::uint32_t error = smoothed > sample ? smoothed - sample : sample - smoothed;
            rttVarianceMs_ = (3U * rttVarianceMs_ + error) / 4U;
            stats_.smoothedRttMilliseconds = (7U * smoothed + sample) / 8U;
        }
    }
    const std::vector<FragmentRef> references = std::move(found->second.fragments);
    sent_.erase(found);
    for (const FragmentRef& reference : references) {
        AckFragment(reference);
    }
}

void Session::AckFragment(const FragmentRef& reference) {
    Channel& channel = channels_[reference.channel];
    if (channel.out.empty()) {
        return;
    }
    const std::uint16_t index = static_cast<std::uint16_t>(reference.messageId - channel.out.front().id);
    if (index >= channel.out.size()) {
        return;
    }
    OutMessage& message = channel.out[index];
    if (message.id != reference.messageId || reference.fragment >= message.fragments.size() || message.fragments[reference.fragment].acked) {
        return;
    }
    message.fragments[reference.fragment].acked = true;
    ++message.ackedFragments;
    while (!channel.out.empty() && channel.out.front().ackedFragments == channel.out.front().fragments.size()) {
        queuedSendBytes_ -= channel.out.front().bytes;
        channel.out.pop_front();
    }
}

bool Session::TimedOut(std::uint64_t nowMilliseconds) const noexcept {
    return nowMilliseconds >= lastReceiveMs_ + config_.timeoutMilliseconds;
}

} // namespace kb::net
