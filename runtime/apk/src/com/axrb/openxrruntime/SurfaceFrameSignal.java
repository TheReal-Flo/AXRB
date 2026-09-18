package com.axrb.openxrruntime;

import android.graphics.SurfaceTexture;
import android.os.Handler;
import android.os.Looper;
import java.util.concurrent.atomic.AtomicBoolean;

/** Tracks producer notifications without relying on producer timestamps. */
public final class SurfaceFrameSignal implements SurfaceTexture.OnFrameAvailableListener {
    private final AtomicBoolean pending = new AtomicBoolean();
    public SurfaceFrameSignal(SurfaceTexture texture) {
        texture.setOnFrameAvailableListener(this, new Handler(Looper.getMainLooper()));
    }
    @Override public void onFrameAvailable(SurfaceTexture texture) { pending.set(true); }
    // Consume before latching. A callback racing with the latch remains pending.
    public boolean consume() { return pending.getAndSet(false); }
}
