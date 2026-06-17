package com.axrb.openxrloaderprobe;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.pm.ApplicationInfo;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;

import java.util.List;

public final class RuntimeBrokerProvider extends ContentProvider {
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

    @Override
    public boolean onCreate() {
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
                appInfo.nativeLibraryDir,
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

        return null;
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
