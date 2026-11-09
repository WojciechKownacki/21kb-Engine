package com.kbengine.runtime;

import android.text.Editable;
import android.text.Selection;
import android.text.SpanWatcher;
import android.text.Spannable;
import android.text.Spanned;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import com.google.androidgamesdk.GameActivity;
import com.google.androidgamesdk.gametextinput.InputConnection;
import com.google.androidgamesdk.gametextinput.State;

// GameTextInput publishes text edits, but its setSelection implementation does
// not publish selection-only changes. Observe the same editable used by the IME.
public class TextInputActivity extends GameActivity {
    @Override protected InputEnabledSurfaceView createSurfaceView() {
        return new InputEnabledSurfaceView(this) {
            private SelectionBridge selectionBridge;

            @Override public InputConnection onCreateInputConnection(EditorInfo info) {
                InputConnection connection = super.onCreateInputConnection(info);
                if (connection != null && (selectionBridge == null || selectionBridge.connection != connection))
                    selectionBridge = new SelectionBridge(this, connection);
                if (selectionBridge != null) selectionBridge.attach();
                return connection;
            }
        };
    }

    private static final class SelectionBridge implements SpanWatcher, Runnable {
        final View view;
        final InputConnection connection;
        boolean pending;

        SelectionBridge(View view, InputConnection connection) {
            this.view = view;
            this.connection = connection;
        }

        void attach() {
            Editable editable = connection.getEditable();
            if (editable.getSpanStart(this) < 0)
                editable.setSpan(this, 0, editable.length(), Spanned.SPAN_INCLUSIVE_INCLUSIVE);
        }

        private void changed(Object span) {
            if (span != Selection.SELECTION_START && span != Selection.SELECTION_END && span != this) return;
            if (!pending) {
                pending = true;
                view.post(this);
            }
        }

        @Override public void run() {
            pending = false;
            attach();
            if (!view.hasWindowFocus() || !connection.getSoftKeyboardActive()) return;
            Editable editable = connection.getEditable();
            int start = Selection.getSelectionStart(editable);
            int end = Selection.getSelectionEnd(editable);
            if (start < 0 || end < 0 || connection.getListener() == null) return;
            connection.getListener().stateChanged(new State(editable.toString(), start, end,
                BaseInputConnection.getComposingSpanStart(editable),
                BaseInputConnection.getComposingSpanEnd(editable)), false);
        }

        @Override public void onSpanAdded(Spannable text, Object span, int start, int end) {
            if (span != this) changed(span);
        }
        @Override public void onSpanRemoved(Spannable text, Object span, int start, int end) { changed(span); }
        @Override public void onSpanChanged(Spannable text, Object span, int oldStart, int oldEnd, int start, int end) {
            changed(span);
        }
    }
}
