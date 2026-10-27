package com.kbengine.runtime;

import com.google.androidgamesdk.GameActivity;
import android.os.Bundle;

public final class MainActivity extends GameActivity {
    static {
        System.loadLibrary("kb_game_android");
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        WindowLayout.configure(this);
    }
}
