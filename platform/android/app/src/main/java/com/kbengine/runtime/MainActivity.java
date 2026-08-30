package com.kbengine.runtime;

import com.google.androidgamesdk.GameActivity;

public final class MainActivity extends GameActivity {
    static {
        System.loadLibrary("kb_game_android");
    }
}
