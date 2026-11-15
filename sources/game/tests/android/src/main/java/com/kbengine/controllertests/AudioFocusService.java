package com.kbengine.controllertests;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;

/**
 * Keeps the competing focus client in a foreground service while a focus test runs. Android 15 grants audio
 * focus to apps that target it only when they are the top app or run a foreground service, and a top activity
 * would pause the app under test instead of taking its focus.
 */
public final class AudioFocusService extends Service {
    private static final String CHANNEL = "focus-test";

    @SuppressWarnings("deprecation")
    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        Notification.Builder builder;
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager notifications = getSystemService(NotificationManager.class);
            notifications.createNotificationChannel(new NotificationChannel(CHANNEL, "Focus test", NotificationManager.IMPORTANCE_LOW));
            builder = new Notification.Builder(this, CHANNEL);
        } else builder = new Notification.Builder(this);
        startForeground(1, builder.setSmallIcon(android.R.drawable.ic_media_play).setContentTitle("Focus test").build());
        return START_NOT_STICKY;
    }

    @Override public IBinder onBind(Intent intent) { return null; }
}
