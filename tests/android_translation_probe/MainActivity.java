package com.axrb.mathprobe;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;

public class MainActivity extends Activity {
    static { System.loadLibrary("mathprobe"); }
    public static native String run(int mode);
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        int mode = getIntent().getIntExtra("mode", 0);
        // Keep long probes off Android's main thread to avoid an unrelated ANR.
        new Thread(() -> {
            Log.i("AXRB.Translation", "BEGIN " + mode);
            Log.i("AXRB.Translation", run(mode));
            runOnUiThread(this::finish);
        }).start();
    }
}
