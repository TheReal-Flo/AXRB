import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.os.Looper;
import android.view.Surface;

// Run with app_process; the main looper delivers real SurfaceTexture callbacks.
public final class SurfaceSmoke {
    static { System.load("/data/local/tmp/libaxrb_surface_smoke.so"); }
    static native Surface create(Context context);
    static native int update();
    static native void destroy();
    static void require(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) throws Exception {
        Looper.prepareMainLooper();
        Class<?> activityThread = Class.forName("android.app.ActivityThread");
        Object thread = activityThread.getMethod("systemMain").invoke(null);
        Context context = (Context) activityThread.getMethod("getSystemContext").invoke(thread);
        new Thread(() -> {
            try {
                Surface producer = create(new android.content.ContextWrapper(context));
                require(producer != null);
                require(update() == 1); // Initial transparent image must be copied.
                for (int i = 0; i < 20; ++i) require(update() == 0);
                for (int color : new int[]{Color.RED, Color.BLUE, Color.GREEN}) {
                    Canvas canvas = producer.lockCanvas(null);
                    canvas.drawColor(color);
                    producer.unlockCanvasAndPost(canvas);
                    boolean changed = false;
                    for (int i = 0; i < 100 && !changed; ++i) {
                        int result = update(); require(result >= 0);
                        changed = result == 1;
                        Thread.sleep(10);
                    }
                    require(changed);
                    // Drain any notification that raced with the previous latch.
                    Thread.sleep(100); require(update() >= 0);
                    for (int i = 0; i < 20; ++i) require(update() == 0);
                }
                producer.release(); destroy();
                System.out.println("PASS: initial transparency, unchanged video, producer updates and resume");
                System.exit(0);
            } catch (Throwable failure) { failure.printStackTrace(); System.exit(1); }
        }, "SurfaceSmoke").start();
        Looper.loop();
    }
}
