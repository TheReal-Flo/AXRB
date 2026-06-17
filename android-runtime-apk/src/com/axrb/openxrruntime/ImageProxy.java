package com.axrb.openxrruntime;

import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.util.Log;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.util.concurrent.atomic.AtomicBoolean;

final class ImageProxy {
    private static final String TAG = "AXRB.ImageProxy";
    private static final String SOCKET_NAME = "axrb_image_proxy";
    private static final int IMAGE_PORT = 38491;
    private static final AtomicBoolean STARTED = new AtomicBoolean(false);

    private ImageProxy() {
    }

    static void ensureStarted() {
        if (!STARTED.compareAndSet(false, true)) {
            return;
        }

        Thread thread = new Thread(new Runnable() {
            @Override
            public void run() {
                serve();
            }
        }, "AXRB-ImageProxy");
        thread.setDaemon(true);
        thread.start();
    }

    private static void serve() {
        try (LocalServerSocket server = new LocalServerSocket(SOCKET_NAME)) {
            Log.i(TAG, "listening localabstract:" + SOCKET_NAME);
            while (true) {
                LocalSocket local = server.accept();
                final LocalSocket client = local;
                Thread clientThread = new Thread(new Runnable() {
                    @Override
                    public void run() {
                        forwardClient(client);
                    }
                }, "AXRB-ImageProxyClient");
                clientThread.setDaemon(true);
                clientThread.start();
            }
        } catch (IOException ex) {
            Log.i(TAG, "server stopped: " + ex);
            STARTED.set(false);
        }
    }

    private static void forwardClient(LocalSocket local) {
        try (LocalSocket localClient = local) {
            Log.i(TAG, "client connected; forwarding to host");
            InputStream input = localClient.getInputStream();
            byte[] header = new byte[64];
            byte[] buffer = new byte[256 * 1024];
            long frameCount = 0;
            Socket frameHost = null;
            while (true) {
                if (!readFully(input, header, 0, header.length)) {
                    return;
                }
                long payloadSize = readLongLE(header, 56);
                if (payloadSize < 0 || payloadSize > 128L * 1024L * 1024L) {
                    Log.i(TAG, "invalid payload size=" + payloadSize);
                    return;
                }

                if (frameHost == null) {
                    frameHost = connectHost();
                    if (frameHost == null) {
                        drain(input, payloadSize, buffer);
                        continue;
                    }
                }

                try {
                    OutputStream output = frameHost.getOutputStream();
                    output.write(header);
                    copyExactly(input, output, payloadSize, buffer);
                    if ((frameCount % 30) == 0) {
                        Log.i(TAG, "forwarded frame bytes=" + payloadSize);
                    }
                    frameCount++;
                } catch (IOException ex) {
                    Log.i(TAG, "host frame forward failed: " + ex);
                    try {
                        frameHost.close();
                    } catch (IOException ignored) {
                    }
                    return;
                }
            }
        } catch (IOException ex) {
            Log.i(TAG, "client stopped: " + ex);
        }
    }

    private static boolean readFully(InputStream input, byte[] buffer, int offset, int size) throws IOException {
        int remaining = size;
        int cursor = offset;
        while (remaining > 0) {
            int read = input.read(buffer, cursor, remaining);
            if (read < 0) {
                return false;
            }
            cursor += read;
            remaining -= read;
        }
        return true;
    }

    private static void copyExactly(InputStream input, OutputStream output, long size, byte[] buffer)
            throws IOException {
        long remaining = size;
        while (remaining > 0) {
            int chunk = (int) Math.min(buffer.length, remaining);
            int read = input.read(buffer, 0, chunk);
            if (read < 0) {
                throw new IOException("local image stream ended during payload");
            }
            output.write(buffer, 0, read);
            remaining -= read;
        }
    }

    private static void drain(InputStream input, long size, byte[] buffer) throws IOException {
        long remaining = size;
        while (remaining > 0) {
            int chunk = (int) Math.min(buffer.length, remaining);
            int read = input.read(buffer, 0, chunk);
            if (read < 0) {
                return;
            }
            remaining -= read;
        }
    }

    private static long readLongLE(byte[] data, int offset) {
        long value = 0;
        for (int i = 7; i >= 0; --i) {
            value = (value << 8) | (data[offset + i] & 0xffL);
        }
        return value;
    }

    private static Socket connectHost() {
        String[] hosts = {"127.0.0.1", "192.168.240.1", "10.0.2.2"};
        for (String hostName : hosts) {
            try {
                Socket socket = new Socket();
                socket.setTcpNoDelay(true);
                socket.setSendBufferSize(1024 * 1024);
                socket.connect(new InetSocketAddress(hostName, IMAGE_PORT), 1000);
                Log.i(TAG, "connected host=" + hostName);
                return socket;
            } catch (IOException ex) {
                Log.i(TAG, "connect failed host=" + hostName + ": " + ex);
            }
        }
        return null;
    }
}
