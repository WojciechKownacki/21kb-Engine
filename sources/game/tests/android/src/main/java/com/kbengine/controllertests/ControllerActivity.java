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

    public boolean dispatchTestTouch(MotionEvent event) {
        return mSurfaceView.dispatchTouchEvent(event);
    }

    public void detachTestSurface() {
        ((android.view.ViewGroup) mSurfaceView.getParent()).removeView(mSurfaceView);
    }

    public void attachTestSurface() {
        setContentView(mSurfaceView);
    }

    public int touchWidth() { return mSurfaceView.getWidth(); }
    public int touchHeight() { return mSurfaceView.getHeight(); }

    @Override protected void onCreate(Bundle state) {
        WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.alpha = 0.0f;
        getWindow().setAttributes(attributes);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
            WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE);
        super.onCreate(state);
    }
}
