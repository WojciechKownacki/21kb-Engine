package com.kbengine.controllertests;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

/** Test-only competing client for actual OS focus notifications. */
public final class AudioFocusReceiver extends BroadcastReceiver {
    private static AudioManager manager;
    private static AudioFocusRequest request;
    private static final AudioManager.OnAudioFocusChangeListener listener = change -> {};

    @SuppressWarnings("deprecation")
    @Override public void onReceive(Context context, Intent intent) {
        if (manager == null) manager = (AudioManager) context.getApplicationContext().getSystemService(Context.AUDIO_SERVICE);
        if (manager == null) { setResultCode(-1); return; }
        String operation = intent.getStringExtra("operation");
        if ("routes".equals(operation)) {
            StringBuilder types = new StringBuilder();
            for (AudioDeviceInfo device : manager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
                if (types.length() != 0) types.append(',');
                types.append(device.getType());
            }
            setResultData("outputTypes=" + types);
            setResultCode(1);
            return;
        }
        if (Build.VERSION.SDK_INT >= 26 && request != null) manager.abandonAudioFocusRequest(request);
        else manager.abandonAudioFocus(listener);
        request = null;
        if ("release".equals(operation)) { setResultCode(1); return; }
        if (!"request".equals(operation)) { setResultCode(-1); return; }
        int gain = intent.getIntExtra("gain", AudioManager.AUDIOFOCUS_GAIN_TRANSIENT);
        int result;
        if (Build.VERSION.SDK_INT >= 26) {
            request = new AudioFocusRequest.Builder(gain)
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setOnAudioFocusChangeListener(listener, new Handler(Looper.getMainLooper())).build();
            result = manager.requestAudioFocus(request);
        } else result = manager.requestAudioFocus(listener, AudioManager.STREAM_MUSIC, gain);
        setResultCode(result);
    }
}
