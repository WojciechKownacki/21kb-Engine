package com.kbengine.controllertests;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

public final class ControllerInstrumentation extends Instrumentation {
    static { System.loadLibrary("kb_controller_tests"); }
    private static native boolean check(int phase, int first, int second);
    private static native void setFocused(boolean focused);
    private static native String diagnostics();
    private static native void holdMotion(boolean hold);
    private static native void consumeTouchFrame();
    private static native boolean touchMatches(int[] ids, int[] phases, float[] coordinates, int width, int height);
    private static native boolean surfaceState(int phase);
    private ControllerActivity activity;

    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }

    private static int find(String name) {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device != null && name.equals(device.getName())) return id;
        }
        return -1;
    }

    private void await(int phase, int first, int second) {
        long until = SystemClock.uptimeMillis() + 10000;
        do {
            if (check(phase, first, second)) return;
            SystemClock.sleep(20);
        } while (SystemClock.uptimeMillis() < until);
        throw new AssertionError("Controller phase " + phase + ": " + diagnostics());
    }

    private void motion(int device, float x, float y, float z, float rz, float left, float right, int hatX, int hatY) {
        MotionEvent.PointerProperties properties = new MotionEvent.PointerProperties();
        properties.id = 0;
        MotionEvent.PointerCoords axes = new MotionEvent.PointerCoords();
        axes.setAxisValue(MotionEvent.AXIS_X, x);
        axes.setAxisValue(MotionEvent.AXIS_Y, y);
        axes.setAxisValue(MotionEvent.AXIS_Z, z);
        axes.setAxisValue(MotionEvent.AXIS_RZ, rz);
        axes.setAxisValue(MotionEvent.AXIS_BRAKE, left);
        axes.setAxisValue(MotionEvent.AXIS_GAS, right);
        axes.setAxisValue(MotionEvent.AXIS_LTRIGGER, left);
        axes.setAxisValue(MotionEvent.AXIS_RTRIGGER, right);
        axes.setAxisValue(MotionEvent.AXIS_HAT_X, hatX);
        axes.setAxisValue(MotionEvent.AXIS_HAT_Y, hatY);
        long now = SystemClock.uptimeMillis();
        MotionEvent event = MotionEvent.obtain(now, now, MotionEvent.ACTION_MOVE, 1,
            new MotionEvent.PointerProperties[]{properties}, new MotionEvent.PointerCoords[]{axes},
            0, 0, 1.0f, 1.0f, device, 0, InputDevice.SOURCE_JOYSTICK, 0);
        runOnMainSync(() -> {
            if (!activity.dispatchControllerMotion(event)) throw new AssertionError("GameActivity rejected controller motion");
        });
        event.recycle();
    }

    private void key(int device, int keyCode, boolean down) {
        long now = SystemClock.uptimeMillis();
        KeyEvent event = new KeyEvent(now, now, down ? KeyEvent.ACTION_DOWN : KeyEvent.ACTION_UP,
            keyCode, 0, 0, device, 0, 0, InputDevice.SOURCE_GAMEPAD);
        runOnMainSync(() -> activity.dispatchKeyEvent(event));
    }

    private void touch(int action, int[] ids, float... coordinates) {
        MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[ids.length];
        MotionEvent.PointerCoords[] points = new MotionEvent.PointerCoords[ids.length];
        for (int index = 0; index < ids.length; ++index) {
            properties[index] = new MotionEvent.PointerProperties();
            properties[index].id = ids[index];
            properties[index].toolType = MotionEvent.TOOL_TYPE_FINGER;
            points[index] = new MotionEvent.PointerCoords();
            points[index].x = coordinates[index * 2];
            points[index].y = coordinates[index * 2 + 1];
            points[index].pressure = 1.0f;
        }
        long now = SystemClock.uptimeMillis();
        MotionEvent event = MotionEvent.obtain(now, now, action, ids.length, properties, points,
            0, 0, 1, 1, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0);
        try {
            if (!activity.dispatchTestTouch(event)) throw new AssertionError("GameActivity rejected touch event");
        } finally { event.recycle(); }
    }

    private void awaitTouches(int[] ids, int[] phases, float[] coordinates, int width, int height) {
        long until = SystemClock.uptimeMillis() + 10000;
        do {
            if (touchMatches(ids, phases, coordinates, width, height)) return;
            SystemClock.sleep(20);
        } while (SystemClock.uptimeMillis() < until);
        throw new AssertionError("Touch snapshot mismatch: " + diagnostics());
    }

    private void awaitTouches(int[] ids, int[] phases, float... coordinates) {
        awaitTouches(ids, phases, coordinates, 0, 0);
    }

    private void awaitSurface(int phase) {
        long until = SystemClock.uptimeMillis() + 10000;
        do {
            if (surfaceState(phase)) return;
            SystemClock.sleep(20);
        } while (SystemClock.uptimeMillis() < until);
        throw new AssertionError("Surface phase " + phase + ": " + diagnostics());
    }

    private int[] extent() {
        int[] result = new int[2];
        runOnMainSync(() -> { result[0] = activity.touchWidth(); result[1] = activity.touchHeight(); });
        return result;
    }

    private void verifyTouches() {
        awaitSurface(0);
        holdMotion(true);
        try {
            runOnMainSync(() -> {
                touch(MotionEvent.ACTION_DOWN, new int[]{7}, 20, 30);
                touch(MotionEvent.ACTION_MOVE, new int[]{7}, 30, 40);
                touch(MotionEvent.ACTION_POINTER_DOWN | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                    new int[]{7, 23}, 30, 40, 70, 80);
            });
        } finally { holdMotion(false); }
        awaitTouches(new int[]{7, 23}, new int[]{0, 0}, 30, 40, 70, 80);
        consumeTouchFrame();
        awaitTouches(new int[]{7, 23}, new int[]{1, 1}, 30, 40, 70, 80);
        holdMotion(true);
        try {
            runOnMainSync(() -> {
                touch(MotionEvent.ACTION_POINTER_UP, new int[]{7, 23}, 35, 45, 75, 85);
                touch(MotionEvent.ACTION_MOVE, new int[]{23}, 90, 100);
            });
        } finally { holdMotion(false); }
        awaitTouches(new int[]{7, 23}, new int[]{2, 1}, 35, 45, 90, 100);
        consumeTouchFrame(); awaitTouches(new int[]{23}, new int[]{1}, 90, 100);
        runOnMainSync(() -> touch(MotionEvent.ACTION_CANCEL, new int[]{23}, 90, 100));
        awaitTouches(new int[]{23}, new int[]{3}, 90, 100);
        consumeTouchFrame(); awaitTouches(new int[]{}, new int[]{});
        runOnMainSync(() -> touch(MotionEvent.ACTION_DOWN, new int[]{7}, 30, 40));
        awaitTouches(new int[]{7}, new int[]{0}, 30, 40);
        setFocused(false); awaitTouches(new int[]{7}, new int[]{3}, 30, 40);
        consumeTouchFrame(); awaitTouches(new int[]{}, new int[]{}); setFocused(true);
        runOnMainSync(() -> touch(MotionEvent.ACTION_MOVE, new int[]{7}, 50, 60));
        awaitTouches(new int[]{}, new int[]{});
        runOnMainSync(() -> touch(MotionEvent.ACTION_DOWN, new int[]{7}, 30, 40));
        awaitTouches(new int[]{7}, new int[]{0}, 30, 40);
        final int[] previousExtent = extent();
        final boolean landscape = previousExtent[0] <= previousExtent[1];
        runOnMainSync(() -> activity.setRequestedOrientation(landscape
            ? ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE : ActivityInfo.SCREEN_ORIENTATION_PORTRAIT));
        long until = SystemClock.uptimeMillis() + 10000;
        int[] size;
        do { size = extent(); if (size[0] > 0 && size[1] > 0 && (size[0] > size[1]) == landscape) break; SystemClock.sleep(20); }
        while (SystemClock.uptimeMillis() < until);
        if (size[0] <= 0 || size[1] <= 0 || (size[0] > size[1]) != landscape)
            throw new AssertionError("Actual orientation and surface resize required");
        awaitTouches(new int[]{7}, new int[]{3}, new float[]{30, 40}, size[0], size[1]);
        consumeTouchFrame(); awaitTouches(new int[]{}, new int[]{});
        float x = size[0] * 0.6f, y = size[1] * 0.4f;
        runOnMainSync(() -> touch(MotionEvent.ACTION_DOWN, new int[]{7}, x, y));
        awaitTouches(new int[]{7}, new int[]{0}, new float[]{x, y}, size[0], size[1]);
        runOnMainSync(() -> activity.detachTestSurface()); awaitSurface(2);
        awaitTouches(new int[]{7}, new int[]{3}, x, y);
        consumeTouchFrame(); awaitTouches(new int[]{}, new int[]{});
        runOnMainSync(() -> activity.attachTestSurface()); awaitSurface(3);
        size = extent();
        final float restoredX = size[0] * 0.25f, restoredY = size[1] * 0.75f;
        runOnMainSync(() -> touch(MotionEvent.ACTION_DOWN, new int[]{23}, restoredX, restoredY));
        awaitTouches(new int[]{23}, new int[]{0}, new float[]{restoredX, restoredY}, size[0], size[1]);
        runOnMainSync(() -> touch(MotionEvent.ACTION_UP, new int[]{23}, restoredX, restoredY));
        awaitTouches(new int[]{23}, new int[]{2}, restoredX, restoredY);
        consumeTouchFrame(); awaitTouches(new int[]{}, new int[]{});
    }

    @Override public void onStart() {
        Bundle report = new Bundle();
        int resultCode = Activity.RESULT_CANCELED;
        try {
            int first, second;
            long registrationDeadline = SystemClock.uptimeMillis() + 10000;
            do {
                first = find("Engine Controller Test A");
                second = find("Engine Controller Test B");
                if (first >= 0 && second >= 0 && first != second) break;
                SystemClock.sleep(20);
            } while (SystemClock.uptimeMillis() < registrationDeadline);
            if (first < 0 || second < 0 || first == second) {
                StringBuilder inventory = new StringBuilder();
                for (int id : InputDevice.getDeviceIds()) inventory.append(InputDevice.getDevice(id));
                throw new AssertionError("Two OS virtual controller fixtures are required: " + inventory);
            }
            Intent intent = new Intent(getTargetContext(), ControllerActivity.class);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            activity = (ControllerActivity) startActivitySync(intent);
            await(0, first, second);
            verifyTouches();
            report.putString("touch", "PASS");
            motion(first, 0.75f, -0.5f, -0.6f, 0.8f, 0.4f, 0.9f, 1, 0);
            key(first, KeyEvent.KEYCODE_BUTTON_A, true);
            await(1, first, second);
            motion(second, -0.8f, 0.6f, 0.5f, -0.7f, 0.7f, 0.2f, -1, -1);
            key(second, KeyEvent.KEYCODE_BUTTON_B, true);
            await(2, first, second);
            motion(first, 0.02f, -0.02f, 0.02f, -0.02f, 0.0f, 0.0f, 0, 0);
            key(first, KeyEvent.KEYCODE_BUTTON_A, false);
            await(3, first, second);
            setFocused(false);
            await(4, first, second);
            setFocused(true);
            await(3, first, second);
            motion(first, 0.7f, -0.4f, -0.3f, 0.9f, 0.5f, 0.8f, 0, 1);
            key(first, KeyEvent.KEYCODE_BUTTON_A, true);
            await(8, first, second);
            Bundle phase = new Bundle(); phase.putString("phase", "disconnect-a");
            sendStatus(1, phase);
            await(5, first, second);
            phase = new Bundle(); phase.putString("phase", "reconnect-a"); sendStatus(1, phase);
            long until = SystemClock.uptimeMillis() + 10000;
            int replacement;
            do {
                replacement = find("Engine Controller Test A");
                if (replacement >= 0 && replacement != first) break;
                SystemClock.sleep(20);
            } while (SystemClock.uptimeMillis() < until);
            if (replacement < 0 || replacement == first) throw new AssertionError("Replacement device ID missing");
            await(6, replacement, second);
            report.putString("result", "PASS");
            resultCode = Activity.RESULT_OK;
        } catch (Throwable error) {
            report.putString("result", "FAIL"); report.putString("error", error.toString());
        } finally {
            if (activity != null) {
                runOnMainSync(() -> activity.finish());
                try { await(7, 0, 0); }
                catch (Throwable error) {
                    report.putString("result", "FAIL"); report.putString("shutdownError", error.toString());
                    resultCode = Activity.RESULT_CANCELED;
                }
            }
            report.putString("evidence", diagnostics()); finish(resultCode, report);
        }
    }
}
