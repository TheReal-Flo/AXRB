package com.axrb.openxrloaderprobe;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

public final class MainActivity extends Activity {
    private static final String TAG = "AXRB.OpenXRLoaderProbe";
    private static final String BUILD_MARKER = "linked-loader-bg-2026-06-15";

    private static native String runProbe(Activity activity);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        TextView textView = new TextView(this);
        textView.setGravity(Gravity.CENTER);
        textView.setTextSize(18.0f);
        textView.setText(BUILD_MARKER + "\nstarting...");
        setContentView(textView);

        new Thread(new Runnable() {
            @Override
            public void run() {
                String result;
                try {
                    System.loadLibrary("openxr_loader_probe");
                    result = BUILD_MARKER + "\n" + runProbe(MainActivity.this);
                } catch (Throwable throwable) {
                    result = BUILD_MARKER + "\nFAIL: " + throwable.getClass().getSimpleName()
                        + ": " + throwable.getMessage();
                }

                Log.i(TAG, result);
                writeResult(result);

                final String finalResult = result;
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        textView.setText(finalResult);
                    }
                });
            }
        }, "AXRB-OpenXR-Probe").start();
    }

    private void writeResult(String result) {
        try (FileOutputStream output = openFileOutput("result.txt", MODE_PRIVATE)) {
            output.write(result.getBytes(StandardCharsets.UTF_8));
            output.write('\n');
        } catch (Exception exception) {
            Log.w(TAG, "Could not write result file", exception);
        }

        File externalDir = getExternalFilesDir(null);
        if (externalDir != null) {
            File externalResult = new File(externalDir, "result.txt");
            try (FileOutputStream output = new FileOutputStream(externalResult)) {
                output.write(result.getBytes(StandardCharsets.UTF_8));
                output.write('\n');
            } catch (Exception exception) {
                Log.w(TAG, "Could not write external result file", exception);
            }
        }
    }
}
