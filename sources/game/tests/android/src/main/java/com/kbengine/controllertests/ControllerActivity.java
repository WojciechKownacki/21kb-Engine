package com.kbengine.controllertests;

import android.os.Bundle;
import android.view.WindowManager;
import android.view.MotionEvent;
import com.google.androidgamesdk.GameActivity;

public final class ControllerActivity extends GameActivity {
    static { System.loadLibrary("kb_controller_tests"); }

    public boolean dispatchControllerMotion(MotionEvent event) {
        return mSurfaceView.dispatchGenericMotionEvent(event);
    }

    @Override protected void onCreate(Bundle state) {
        WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.alpha = 0.0f;
        getWindow().setAttributes(attributes);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
            WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE);
        super.onCreate(state);
    }
}
