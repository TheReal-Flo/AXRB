package com.axrb.openxrruntime;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.pm.ApplicationInfo;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.SystemClock;
import android.util.Log;

import java.io.DataInputStream;
import java.io.File;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.util.List;

public final class RuntimeBrokerProvider extends ContentProvider {
    private static final String TAG = "AXRB.PoseBroker";
    private static final int BRIDGE_PORT = 38490;
    private static final int POSE_FRAME_BYTES = 112;
    private static final int POSE_MAGIC = 0x42525841;
    private static final short POSE_VERSION = 1;
    private static final short POSE_TYPE = 1;

    private static final String[] ACTIVE_RUNTIME_COLUMNS = {
        "package_name",
        "native_lib_dir",
        "so_filename",
        "has_functions"
    };

    private static final String[] FUNCTION_COLUMNS = {
        "function_name",
        "symbol_name"
    };

    private static final String[] POSE_COLUMNS = {
        "sequence",
        "monotonic_time_ns",
        "hmd_x",
        "hmd_y",
        "hmd_z",
        "hmd_qx",
        "hmd_qy",
        "hmd_qz",
        "hmd_qw",
        "left_x",
        "left_y",
        "left_z",
        "left_qx",
        "left_qy",
        "left_qz",
        "left_qw",
        "right_x",
        "right_y",
        "right_z",
        "right_qx",
        "right_qy",
        "right_qz",
        "right_qw"
    };

    private final Object poseLock = new Object();
    private Socket socket;
    private DataInputStream input;
    private long lastConnectAttemptMs;
    private long sequence;
    private long monotonicTimeNs;
    private final float[] hmd = {0.0f, 1.65f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    private final float[] left = {-0.25f, 1.25f, -0.45f, 0.0f, 0.0f, 0.0f, 1.0f};
    private final float[] right = {0.25f, 1.25f, -0.45f, 0.0f, 0.0f, 0.0f, 1.0f};

    @Override
    public boolean onCreate() {
        ImageProxy.ensureStarted();
        return true;
    }

    @Override
    public Cursor query(
            Uri uri,
            String[] projection,
            String selection,
            String[] selectionArgs,
            String sortOrder) {
        List<String> segments = uri.getPathSegments();
        if (segments.size() >= 6
                && "openxr".equals(segments.get(0))
                && "1".equals(segments.get(1))
                && "abi".equals(segments.get(2))
                && "runtimes".equals(segments.get(4))
                && "active".equals(segments.get(5))) {
            MatrixCursor cursor = new MatrixCursor(ACTIVE_RUNTIME_COLUMNS);
            ApplicationInfo appInfo = getContext().getApplicationInfo();
            cursor.addRow(new Object[] {
                getContext().getPackageName(),
                getRuntimeNativeLibDir(appInfo, segments.get(3)),
                "libopenxr_runtime.so",
                0
            });
            return cursor;
        }

        if (segments.size() >= 7
                && "openxr".equals(segments.get(0))
                && "1".equals(segments.get(1))
                && "abi".equals(segments.get(2))
                && "runtimes".equals(segments.get(4))
                && "functions".equals(segments.get(6))) {
            return new MatrixCursor(FUNCTION_COLUMNS);
        }

        if (segments.size() >= 4
                && "openxr".equals(segments.get(0))
                && "1".equals(segments.get(1))
                && "pose".equals(segments.get(2))
                && "latest".equals(segments.get(3))) {
            updatePose();
            MatrixCursor cursor = new MatrixCursor(POSE_COLUMNS);
            synchronized (poseLock) {
                cursor.addRow(new Object[] {
                    sequence,
                    monotonicTimeNs,
                    hmd[0],
                    hmd[1],
                    hmd[2],
                    hmd[3],
                    hmd[4],
                    hmd[5],
                    hmd[6],
                    left[0],
                    left[1],
                    left[2],
                    left[3],
                    left[4],
                    left[5],
                    left[6],
                    right[0],
                    right[1],
                    right[2],
                    right[3],
                    right[4],
                    right[5],
                    right[6]
                });
            }
            return cursor;
        }

        return null;
    }

    private static String getRuntimeNativeLibDir(ApplicationInfo appInfo, String abi) {
        String systemDir = getSystemRuntimeNativeLibDir(appInfo, abi);
        if (systemDir != null) {
            return systemDir;
        }
        return appInfo.nativeLibraryDir;
    }

    private static String getSystemRuntimeNativeLibDir(ApplicationInfo appInfo, String abi) {
        if (appInfo.sourceDir == null) {
            return null;
        }

        File appDir = new File(appInfo.sourceDir).getParentFile();
        if (appDir == null) {
            return null;
        }

        String libSubdir = getSystemRuntimeLibSubdir(abi);
        if (libSubdir == null) {
            return null;
        }

        File candidate = new File(new File(appDir, "lib"), libSubdir);
        if (new File(candidate, "libopenxr_runtime.so").isFile()) {
            return candidate.getAbsolutePath();
        }

        return null;
    }

    private static String getSystemRuntimeLibSubdir(String abi) {
        if ("arm64-v8a".equals(abi)) {
            return "arm64";
        }
        if ("armeabi-v7a".equals(abi) || "armeabi".equals(abi)) {
            return "arm";
        }
        if ("x86_64".equals(abi)) {
            return "x86_64";
        }
        if ("x86".equals(abi)) {
            return "x86";
        }
        return null;
    }

    private void updatePose() {
        synchronized (poseLock) {
            if (!ensureConnectedLocked()) {
                return;
            }

            try {
                byte[] frame = new byte[POSE_FRAME_BYTES];
                input.readFully(frame);
                if (readIntLE(frame, 0) != POSE_MAGIC
                        || readShortLE(frame, 4) != POSE_VERSION
                        || readShortLE(frame, 6) != POSE_TYPE) {
                    closeSocketLocked();
                    return;
                }

                sequence = readLongLE(frame, 8);
                monotonicTimeNs = readLongLE(frame, 16);
                readPose(frame, 24, hmd);
                readPose(frame, 52, left);
                readPose(frame, 80, right);
            } catch (IOException ex) {
                Log.i(TAG, "pose read failed: " + ex);
                closeSocketLocked();
            }
        }
    }

    private boolean ensureConnectedLocked() {
        if (socket != null && socket.isConnected() && !socket.isClosed()) {
            return true;
        }

        long nowMs = SystemClock.elapsedRealtime();
        if (nowMs - lastConnectAttemptMs < 1000) {
            return false;
        }
        lastConnectAttemptMs = nowMs;

        String[] hosts = {"172.26.32.1", "127.0.0.1", "192.168.240.1", "10.0.2.2"};
        for (String host : hosts) {
            try {
                Socket candidate = new Socket();
                candidate.connect(new InetSocketAddress(host, BRIDGE_PORT), 1000);
                candidate.setSoTimeout(1000);
                socket = candidate;
                input = new DataInputStream(candidate.getInputStream());
                Log.i(TAG, "connected host=" + host);
                return true;
            } catch (IOException ex) {
                Log.i(TAG, "connect failed host=" + host + ": " + ex);
            }
        }

        return false;
    }

    private void closeSocketLocked() {
        try {
            if (socket != null) {
                socket.close();
            }
        } catch (IOException ignored) {
        }
        socket = null;
        input = null;
    }

    private static void readPose(byte[] frame, int offset, float[] out) {
        for (int i = 0; i < 7; ++i) {
            out[i] = Float.intBitsToFloat(readIntLE(frame, offset + i * 4));
        }
    }

    private static short readShortLE(byte[] data, int offset) {
        return (short) ((data[offset] & 0xff) | ((data[offset + 1] & 0xff) << 8));
    }

    private static int readIntLE(byte[] data, int offset) {
        return (data[offset] & 0xff)
                | ((data[offset + 1] & 0xff) << 8)
                | ((data[offset + 2] & 0xff) << 16)
                | ((data[offset + 3] & 0xff) << 24);
    }

    private static long readLongLE(byte[] data, int offset) {
        long value = 0;
        for (int i = 7; i >= 0; --i) {
            value = (value << 8) | (data[offset + i] & 0xffL);
        }
        return value;
    }

    @Override
    public String getType(Uri uri) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        return 0;
    }
}
