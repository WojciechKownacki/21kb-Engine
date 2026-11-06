local connected = 0
local disconnected = 0
local spawned = 0
local despawned = 0
local replies = 0
local frames = 0
local sent = false

function Ready(self, dt)
    CallFunction("Net.RegisterRpc", {name="Echo", direction="toServer", args="string(32)"})
    CallFunction("Net.RegisterRpc", {name="EchoReply", direction="toClients", args="string(32)"})
end

function OnNetConnected(self, event)
    connected = connected + 1
    sent = false
end

function OnNetDisconnected(self, event)
    disconnected = disconnected + 1
    sent = false
end

function OnNetSpawned(self, event)
    spawned = spawned + 1
end

function OnNetDespawned(self, event)
    despawned = despawned + 1
end

function OnRpc_Echo(self, event)
    CallFunction("Net.Call", {rpc="EchoReply", target=event.args.sender, s1=event.args.s1})
end

function OnRpc_EchoReply(self, event)
    if event.args.s1 == "runtime" then replies = replies + 1 end
end

function Tick(self, dt)
    frames = frames + 1
    if CallFunction("Net.IsServer", {}) then return end
    if connected > disconnected and not sent then
        sent = CallFunction("Net.Call", {rpc="Echo", s1="runtime"})
    end
    if frames % 8 == 0 then
        Log(string.format('network-flow {"connected":%d,"disconnected":%d,"spawned":%d,"despawned":%d,"replies":%d}',
            connected, disconnected, spawned, despawned, replies))
    end
end
