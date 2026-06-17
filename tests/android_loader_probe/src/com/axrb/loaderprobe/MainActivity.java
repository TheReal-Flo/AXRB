package com.axrb.loaderprobe;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private static final String TAG = "AXRB.LoaderProbe";

    static {
        System.loadLibrary("loader_probe");
    }

    private static native String runProbe();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        String result;
        try {
            result = runProbe();
        } catch (Throwable throwable) {
            result = "FAIL: " + throwable.getClass().getSimpleName() + ": " + throwable.getMessage();
        }

        Log.i(TAG, result);

        TextView textView = new TextView(this);
        textView.setGravity(Gravity.CENTER);
        textView.setTextSize(18.0f);
        textView.setText(result);
        setContentView(textView);
    }
}
