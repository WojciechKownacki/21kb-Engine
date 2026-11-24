package com.kbengine.runtime;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkRequest;
import android.os.Build;
import android.util.Log;

import java.util.Collections;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

// Reports whether the device has a network to the native host. The answer is a boolean because the game only needs
// to know whether to keep trying: a new network means a new handshake, no network means none.
final class NetworkMonitor {
    private static native void onNetworkChanged(boolean available);

    private final ConnectivityManager manager;
    private final Set<Network> networks = Collections.newSetFromMap(new ConcurrentHashMap<>());
    private ConnectivityManager.NetworkCallback callback;
    private boolean known;
    private boolean reported;

    NetworkMonitor(Context context) {
        manager = (ConnectivityManager) context.getApplicationContext().getSystemService(Context.CONNECTIVITY_SERVICE);
    }

    void start() {
        if (manager == null || callback != null) return;
        callback = new ConnectivityManager.NetworkCallback() {
            @Override public void onAvailable(Network network) {
                networks.add(network);
                report(true);
            }

            @Override public void onLost(Network network) {
                networks.remove(network);
                report(!networks.isEmpty());
            }
        };
        try {
            if (Build.VERSION.SDK_INT >= 24) {
                manager.registerDefaultNetworkCallback(callback);
            } else {
                manager.registerNetworkCallback(
                    new NetworkRequest.Builder().addCapability(android.net.NetworkCapabilities.NET_CAPABILITY_INTERNET).build(),
                    callback);
            }
        } catch (RuntimeException error) {
            // The manifest grants ACCESS_NETWORK_STATE only to a project that uses the network.
            Log.w("21kb", "network monitor unavailable: " + error.getMessage());
            callback = null;
        }
    }

    void stop() {
        if (manager != null && callback != null) {
            try {
                manager.unregisterNetworkCallback(callback);
            } catch (RuntimeException ignored) {
            }
        }
        callback = null;
        networks.clear();
        known = false;
        reported = false;
    }

    private void report(boolean available) {
        // Every onAvailable is a new network generation (Wi-Fi -> mobile arrives before the old network is lost), so a
        // true is always reported; only a repeated false is dropped.
        if (!available && known && !reported) return;
        known = true;
        reported = available;
        onNetworkChanged(available);
    }
}
