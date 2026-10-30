package com.kbengine.controllertests;

import android.os.Bundle;
import android.view.WindowManager;
import android.view.MotionEvent;
import com.kbengine.runtime.TextInputActivity;
import com.kbengine.runtime.WindowLayout;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.graphics.Insets;

public final class ControllerActivity extends TextInputActivity {
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

    public android.view.inputmethod.InputConnection textConnection() {
        return mSurfaceView.onCreateInputConnection(new android.view.inputmethod.EditorInfo());
    }

    public boolean keyboardVisible() {
        WindowInsetsCompat insets = ViewCompat.getRootWindowInsets(mSurfaceView);
        return insets != null && insets.isVisible(WindowInsetsCompat.Type.ime());
    }

    public String imeGeometry() {
        WindowInsetsCompat insets = ViewCompat.getRootWindowInsets(mSurfaceView);
        android.graphics.Rect visible = new android.graphics.Rect();
        mSurfaceView.getWindowVisibleDisplayFrame(visible);
        return "surface=" + touchWidth() + "x" + touchHeight() + " decor=" +
            getWindow().getDecorView().getWidth() + "x" + getWindow().getDecorView().getHeight() +
            " ime=" + (insets == null ? "missing" : insets.getInsets(WindowInsetsCompat.Type.ime()).toString()) +
            " visible=" + visible;
    }

    public int[] safeArea() {
        WindowInsetsCompat windowInsets = ViewCompat.getRootWindowInsets(mSurfaceView);
        if (windowInsets == null) return null;
        Insets insets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars() |
            WindowInsetsCompat.Type.displayCutout() | WindowInsetsCompat.Type.mandatorySystemGestures() |
            WindowInsetsCompat.Type.ime());
        Insets waterfall = windowInsets.getDisplayCutout() == null ? Insets.NONE :
            windowInsets.getDisplayCutout().getWaterfallInsets();
        int[] location = new int[2];
        mSurfaceView.getLocationInWindow(location);
        if (location[0] != 0 || location[1] != 0 ||
            touchWidth() != getWindow().getDecorView().getWidth() ||
            touchHeight() != getWindow().getDecorView().getHeight()) return null;
        return new int[]{Math.max(insets.left, waterfall.left), Math.max(insets.top, waterfall.top),
            Math.max(insets.right, waterfall.right), Math.max(insets.bottom, waterfall.bottom)};
    }

    @Override protected void onCreate(Bundle state) {
        if (!getIntent().getBooleanExtra("ime", false)) {
            WindowManager.LayoutParams attributes = getWindow().getAttributes();
            attributes.alpha = 0.0f;
            getWindow().setAttributes(attributes);
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE);
        }
        super.onCreate(state);
        WindowLayout.configure(this);
    }
}
