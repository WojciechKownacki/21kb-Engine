package com.kbengine.runtime;

import android.os.Bundle;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.util.Log;

public final class MainActivity extends TextInputActivity {
    private static native void setAudioFocusGranted(boolean granted);
    private AudioManager audioManager;
    private AudioFocusRequest audioRequest;
    private boolean audioEnabled;
    private boolean resumed;
    private boolean focusRequested;
    private final AudioDeviceCallback audioDevices = new AudioDeviceCallback() {
        @Override public void onAudioDevicesAdded(AudioDeviceInfo[] devices) { reportRoutes("added", devices); }
        @Override public void onAudioDevicesRemoved(AudioDeviceInfo[] devices) { reportRoutes("removed", devices); }
    };
    private static void reportRoutes(String event, AudioDeviceInfo[] devices) {
        for (AudioDeviceInfo device : devices) {
            if (device.isSink()) Log.i("21kb", "audio route " + event + " id=" + device.getId() + " type=" + device.getType());
        }
    }
    private final AudioManager.OnAudioFocusChangeListener audioListener = change -> {
        if (change == AudioManager.AUDIOFOCUS_LOSS) focusRequested = false;
        boolean granted = audioEnabled && resumed && change == AudioManager.AUDIOFOCUS_GAIN;
        setAudioFocusGranted(granted);
        Log.i("21kb", "audio focus change=" + change + " granted=" + granted);
    };
    static {
        System.loadLibrary("kb_game_android");
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        WindowLayout.configure(this);
        audioManager = (AudioManager) getSystemService(AUDIO_SERVICE);
        if (audioManager != null) audioManager.registerAudioDeviceCallback(audioDevices, new Handler(Looper.getMainLooper()));
    }

    // Called from the native owner thread; AudioManager policy stays on the UI thread.
    public void setAudioEnabled(boolean enabled) {
        runOnUiThread(() -> {
            audioEnabled = enabled;
            if (enabled && resumed) requestAudio();
            else if (!enabled) releaseAudio();
        });
    }

    @SuppressWarnings("deprecation")
    private void requestAudio() {
        if (!audioEnabled || !resumed || focusRequested || audioManager == null) return;
        int result;
        if (Build.VERSION.SDK_INT >= 26) {
            if (audioRequest == null) {
                audioRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                    .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                    .setAcceptsDelayedFocusGain(true).setWillPauseWhenDucked(true)
                    .setOnAudioFocusChangeListener(audioListener, new Handler(Looper.getMainLooper())).build();
            }
            result = audioManager.requestAudioFocus(audioRequest);
        } else {
            result = audioManager.requestAudioFocus(audioListener, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN);
        }
        focusRequested = result != AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        setAudioFocusGranted(result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED);
        Log.i("21kb", "audio focus request=" + result);
    }

    @SuppressWarnings("deprecation")
    private void releaseAudio() {
        setAudioFocusGranted(false);
        if (focusRequested && audioManager != null) {
            if (Build.VERSION.SDK_INT >= 26 && audioRequest != null) audioManager.abandonAudioFocusRequest(audioRequest);
            else audioManager.abandonAudioFocus(audioListener);
        }
        focusRequested = false;
    }

    @Override protected void onResume() {
        super.onResume();
        resumed = true;
        requestAudio();
    }

    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) requestAudio();
    }

    @Override protected void onPause() {
        resumed = false;
        releaseAudio();
        super.onPause();
    }

    @Override protected void onDestroy() {
        audioEnabled = false;
        releaseAudio();
        if (audioManager != null) audioManager.unregisterAudioDeviceCallback(audioDevices);
        super.onDestroy();
    }
}
