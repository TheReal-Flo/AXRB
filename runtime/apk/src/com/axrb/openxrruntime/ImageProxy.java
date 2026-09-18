package com.axrb.openxrruntime;

import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.util.Log;

import java.io.IOException;
import java.io.InputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.util.concurrent.atomic.AtomicBoolean;

final class ImageProxy {
    private static final String TAG = "AXRB.ImageProxy";
    private static final String SOCKET_NAME = "axrb_image_proxy";
    private static final int VIDEO_PORT = 38492;
    private static final int VIDEO_PACKET_HEADER_SIZE = 64;
    private static final int VIDEO_PACKET_PAYLOAD_MAX = 1200;
    private static final int VIDEO_MAGIC = 0x56525841; // AXRV
    private static final int VIDEO_CODEC_AXRB_RGBA8 = 100;
    private static final int VIDEO_FLAG_KEY_FRAME = 1;
    private static final int VIDEO_FLAG_END_OF_FRAME = 2;
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
            Log.i(TAG, "client connected; forwarding app frames over UDP video");
            InputStream input = localClient.getInputStream();
            byte[] header = new byte[64];
            byte[] buffer = new byte[256 * 1024];
            long frameCount = 0;
            UdpVideoHost frameHost = null;
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
                    frameHost = connectVideoHost();
                    if (frameHost == null) {
                        drain(input, payloadSize, buffer);
                        continue;
                    }
                }

                try {
                    byte[] payload = readPayload(input, payloadSize, buffer);
                    int width = readIntLE(header, 12);
                    int height = readIntLE(header, 16);
                    long sequence = readLongLE(header, 40);
                    long monotonicTimeNs = readLongLE(header, 48);
                    frameHost.sendFrame(sequence, monotonicTimeNs, width, height, payload);
                    if ((frameCount % 30) == 0) {
                        Log.i(TAG, "sent UDP video frame bytes=" + payloadSize);
                    }
                    frameCount++;
                } catch (IOException ex) {
                    Log.i(TAG, "host UDP video forward failed: " + ex);
                    frameHost.close();
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

    private static byte[] readPayload(InputStream input, long size, byte[] buffer) throws IOException {
        if (size > Integer.MAX_VALUE) {
            throw new IOException("payload too large for UDP video frame");
        }

        byte[] payload = new byte[(int) size];
        long remaining = size;
        int cursor = 0;
        while (remaining > 0) {
            int chunk = (int) Math.min(buffer.length, remaining);
            int read = input.read(buffer, 0, chunk);
            if (read < 0) {
                throw new IOException("local image stream ended during payload");
            }
            System.arraycopy(buffer, 0, payload, cursor, read);
            cursor += read;
            remaining -= read;
        }
        return payload;
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

    private static int readIntLE(byte[] data, int offset) {
        return (data[offset] & 0xff)
                | ((data[offset + 1] & 0xff) << 8)
                | ((data[offset + 2] & 0xff) << 16)
                | ((data[offset + 3] & 0xff) << 24);
    }

    private static void writeShortLE(byte[] data, int offset, int value) {
        data[offset] = (byte) (value & 0xff);
        data[offset + 1] = (byte) ((value >>> 8) & 0xff);
    }

    private static void writeIntLE(byte[] data, int offset, int value) {
        data[offset] = (byte) (value & 0xff);
        data[offset + 1] = (byte) ((value >>> 8) & 0xff);
        data[offset + 2] = (byte) ((value >>> 16) & 0xff);
        data[offset + 3] = (byte) ((value >>> 24) & 0xff);
    }

    private static void writeLongLE(byte[] data, int offset, long value) {
        for (int i = 0; i < 8; ++i) {
            data[offset + i] = (byte) ((value >>> (i * 8)) & 0xff);
        }
    }

    private static UdpVideoHost connectVideoHost() {
        String[] hosts = {"172.26.32.1", "127.0.0.1", "192.168.240.1", "10.0.2.2"};
        for (String hostName : hosts) {
            try {
                DatagramSocket socket = new DatagramSocket();
                socket.setSendBufferSize(4 * 1024 * 1024);
                InetAddress address = InetAddress.getByName(hostName);
                Log.i(TAG, "connected UDP video host=" + hostName);
                return new UdpVideoHost(socket, address, VIDEO_PORT);
            } catch (IOException ex) {
                Log.i(TAG, "UDP video host failed host=" + hostName + ": " + ex);
            }
        }
        return null;
    }

    private static final class UdpVideoHost {
        private final DatagramSocket socket;
        private final InetAddress address;
        private final int port;

        UdpVideoHost(DatagramSocket socket, InetAddress address, int port) {
            this.socket = socket;
            this.address = address;
            this.port = port;
        }

        void sendFrame(long sequence, long monotonicTimeNs, int width, int height, byte[] payload)
                throws IOException {
            int offset = 0;
            while (offset < payload.length) {
                int chunk = Math.min(VIDEO_PACKET_PAYLOAD_MAX, payload.length - offset);
                byte[] packet = new byte[VIDEO_PACKET_HEADER_SIZE + chunk];
                writeIntLE(packet, 0, VIDEO_MAGIC);
                writeShortLE(packet, 4, 1);
                writeShortLE(packet, 6, VIDEO_PACKET_HEADER_SIZE);
                writeIntLE(packet, 8, 1);
                writeIntLE(packet, 12, VIDEO_CODEC_AXRB_RGBA8);
                writeLongLE(packet, 16, sequence);
                writeLongLE(packet, 24, monotonicTimeNs);
                writeIntLE(packet, 32, width);
                writeIntLE(packet, 36, height);
                writeIntLE(packet, 40, payload.length);
                writeIntLE(packet, 44, offset);
                writeShortLE(packet, 48, chunk);
                int flags = VIDEO_FLAG_KEY_FRAME;
                if (offset + chunk == payload.length) {
                    flags |= VIDEO_FLAG_END_OF_FRAME;
                }
                writeShortLE(packet, 50, flags);
                System.arraycopy(payload, offset, packet, VIDEO_PACKET_HEADER_SIZE, chunk);
                socket.send(new DatagramPacket(packet, packet.length, address, port));
                offset += chunk;
            }
        }

        void close() {
            socket.close();
        }
    }
}
