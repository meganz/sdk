package nz.mega.sdk;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Matrix;
import android.graphics.Rect;
import android.media.MediaMetadataRetriever;
import android.media.ThumbnailUtils;
import android.net.Uri;
import android.provider.BaseColumns;
import android.provider.MediaStore;
import androidx.exifinterface.media.ExifInterface;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URLConnection;
import java.util.Objects;

public class AndroidGfxProcessor extends MegaGfxProcessor {
    Rect size;
    int orientation;
    String srcPath;
    Bitmap bitmap;
    byte[] bitmapData;
    static Context context = null;

    protected AndroidGfxProcessor() {
        if (context == null) {
            try {
                context = (Context) Class.forName("android.app.AppGlobals")
                        .getMethod("getInitialApplication")
                        .invoke(null, (Object[]) null);
            } catch (Exception e) {
            }
        }
    }

    public static boolean isVideoFile(String path) {
        try {
            String mimeType = URLConnection.guessContentTypeFromName(path);
            if (mimeType == null) {
                Uri uri = Uri.parse(path);
                mimeType = context.getContentResolver().getType(uri);
            }
            return mimeType != null && mimeType.startsWith("video");
        } catch (Exception e) {
            return false;
        }
    }

    public static Rect getImageDimensions(String path, int orientation) {
        Rect rect = new Rect();

        if(isVideoFile(path)){
            MediaMetadataRetriever retriever = new MediaMetadataRetriever();

            try {
                setMediaMetadataRetrieverDataSource(retriever, path);
                int width;
                int height;
                int interchangeOrientation = Integer.parseInt(Objects.requireNonNull(
                    retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_ROTATION)));
                if (interchangeOrientation == 90 || interchangeOrientation == 270) {
                    width = Integer.parseInt(Objects.requireNonNull(retriever.extractMetadata(
                        MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)));
                    height = Integer.parseInt(Objects.requireNonNull(retriever.extractMetadata(
                        MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)));
                }
                else {
                    width = Integer.parseInt(Objects.requireNonNull(retriever.extractMetadata(
                        MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)));
                    height = Integer.parseInt(Objects.requireNonNull(retriever.extractMetadata(
                        MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)));
                }

                rect.right = width;
                rect.bottom = height;
            } catch (Exception e) {
                System.out.println("Error in getImageDimensions for video: " + e);
            } finally {
                try {
                    retriever.release();
                } catch (IOException e) {
                    System.out.println("Error releasing MediaMetadataRetriever for video: " + e);
                }
            }
        }
        else{
            try {
                BitmapFactory.Options options = new BitmapFactory.Options();
                options.inJustDecodeBounds = true;
                InputStream inputStream = getInputStreamFromPath(path);
                BitmapFactory.decodeStream(inputStream, null, options);

                if ((options.outWidth > 0) && (options.outHeight > 0)) {
                    if ((orientation < 5) || (orientation > 8)) {
                        rect.right = options.outWidth;
                        rect.bottom = options.outHeight;
                    } else {
                        rect.bottom = options.outWidth;
                        rect.right = options.outHeight;
                    }
                }
            } catch (Exception e) {
            }
        }

        return rect;
    }

    private static InputStream getInputStreamFromPath(String path) {
        try {
            Uri uri = Uri.parse(path);
            String scheme = uri == null ? null : uri.getScheme();
            if (scheme != null && scheme.equals("content")) {
                return context.getContentResolver().openInputStream(uri);
            } else {
                return new FileInputStream(path);
            }
        } catch (Exception e) {
            return null;
        }
    }

    private static void setMediaMetadataRetrieverDataSource(MediaMetadataRetriever retriever,
                                                            String path) {
        Uri uri = Uri.parse(path);
        String scheme = uri == null ? null : uri.getScheme();
        if (scheme != null && scheme.equals("content")) {
            retriever.setDataSource(context, uri);
        } else {
            retriever.setDataSource(path);
        }
    }

    public boolean readBitmap(String path) {

        if(isVideoFile(path)){
            srcPath = path;
            size = getImageDimensions(srcPath, orientation);
            return (size.right != 0) && (size.bottom != 0);
        }
        else{
            srcPath = path;
            orientation = getExifOrientation(path);
            size = getImageDimensions(srcPath, orientation);
            return (size.right != 0) && (size.bottom != 0);
        }
    }

    public int getWidth() {
        return size.right;
    }

    public int getHeight() {
        return size.bottom;
    }

    static public Bitmap getBitmap(String path, Rect rect, int orientation, int w, int h) {
        int width;
        int height;

        if (isVideoFile(path)) {
            Uri uri = Uri.parse(path);
            String scheme = uri == null ? null : uri.getScheme();
            boolean isContentUri = scheme != null && scheme.equals("content");

            Bitmap bmThumbnail = null;
            Cursor cursor = null;

            if (!isContentUri) {
                try {
                    bmThumbnail = ThumbnailUtils.createVideoThumbnail(
                        path, MediaStore.Video.Thumbnails.FULL_SCREEN_KIND);
                    if (context != null && bmThumbnail == null) {

                        String SELECTION = MediaStore.MediaColumns.DATA + "=?";
                        String[] PROJECTION = {BaseColumns._ID};

                        uri = MediaStore.Video.Media.EXTERNAL_CONTENT_URI;
                        String[] selectionArgs = {path};
                        ContentResolver cr = context.getContentResolver();
                        cursor = cr.query(uri, PROJECTION, SELECTION, selectionArgs, null);
                        if (cursor != null && cursor.moveToFirst()) {
                            long videoId = cursor.getLong(0);
                            bmThumbnail = MediaStore.Video.Thumbnails.getThumbnail(
                                cr, videoId, MediaStore.Video.Thumbnails.FULL_SCREEN_KIND, null);
                        }
                    }
                } catch (Exception e) {
                    e.printStackTrace();
                } finally {
                    if (cursor != null) {
                        cursor.close();
                    }
                }
            }

            if(bmThumbnail==null){

                MediaMetadataRetriever retriever = new MediaMetadataRetriever();
                try{
                    setMediaMetadataRetrieverDataSource(retriever, path);
                    bmThumbnail = retriever.getFrameAtTime();
                } catch (Exception e1) {
                    e1.printStackTrace();
                } finally {
                    try {
                        retriever.release();
                    } catch (Exception ex) {
                        ex.printStackTrace();
                    }
                }
            }

            if (!isContentUri && bmThumbnail == null) {
                try{
                    bmThumbnail = ThumbnailUtils.createVideoThumbnail(path, MediaStore.Video.Thumbnails.MINI_KIND);
                    if(context != null && bmThumbnail == null) {

                        String SELECTION = MediaStore.MediaColumns.DATA + "=?";
                        String[] PROJECTION = {BaseColumns._ID};

                        uri = MediaStore.Video.Media.EXTERNAL_CONTENT_URI;
                        String[] selectionArgs = {path};
                        ContentResolver cr = context.getContentResolver();
                        cursor = cr.query(uri, PROJECTION, SELECTION, selectionArgs, null);
                        if (cursor != null && cursor.moveToFirst()) {
                            long videoId = cursor.getLong(0);
                            bmThumbnail = MediaStore.Video.Thumbnails.getThumbnail(cr, videoId, MediaStore.Video.Thumbnails.MINI_KIND, null);
                        }
                    }
                } catch (Exception e2) {
                    e2.printStackTrace();
                } finally {
                    if (cursor != null) {
                        cursor.close();
                    }
                }
            }

            try {
                if (bmThumbnail != null) {
                    return Bitmap.createScaledBitmap(bmThumbnail, w, h, true);
                }
            }catch (Exception e){
            }
        }
        else{
            if ((orientation < 5) || (orientation > 8)) {
                width = rect.right;
                height = rect.bottom;
            } else {
                width = rect.bottom;
                height = rect.right;
            }

            try {
                int scale = 1;
                while (width / scale / 2 >= w && height / scale / 2 >= h)
                    scale *= 2;

                BitmapFactory.Options options = new BitmapFactory.Options();
                options.inJustDecodeBounds = false;
                options.inSampleSize = scale;
                InputStream inputStream = getInputStreamFromPath(path);
                Bitmap tmp = BitmapFactory.decodeStream(inputStream, null, options);
                tmp = fixExifOrientation(tmp, orientation);
                return Bitmap.createScaledBitmap(tmp, w, h, true);
            } catch (Exception e) {
            }
        }

        return null;
    }


    public static int getExifOrientation(String srcPath) {
        int orientation = ExifInterface.ORIENTATION_UNDEFINED;

        int i = 0;
        while ((i < 5) && (orientation == ExifInterface.ORIENTATION_UNDEFINED)) {
            try {
                ExifInterface exif = new ExifInterface(srcPath);
                orientation = exif.getAttributeInt(ExifInterface.TAG_ORIENTATION, orientation);
            } catch (IOException e) {
                try {
                    Thread.sleep(100);
                } catch (InterruptedException e1) {}
            }

            i++;
        }
        return orientation;
    }

    /*
     * Change image orientation based on EXIF image data
     */
    public static Bitmap fixExifOrientation(Bitmap bitmap, int orientation) {
        if (bitmap == null)
            return null;

        if ((orientation < 2) || (orientation > 8)) {
            // No changes required or invalid orientation
            return bitmap;
        }

        Matrix matrix = new Matrix();
        switch (orientation) {
        case ExifInterface.ORIENTATION_TRANSPOSE:
        case ExifInterface.ORIENTATION_ROTATE_90:
            matrix.postRotate(90);
            break;
        case ExifInterface.ORIENTATION_ROTATE_180:
        case ExifInterface.ORIENTATION_FLIP_VERTICAL:
            matrix.postRotate(180);
            break;
        case ExifInterface.ORIENTATION_TRANSVERSE:
        case ExifInterface.ORIENTATION_ROTATE_270:
            matrix.postRotate(270);
            break;
        default:
            break;
        }

        if ((orientation == ExifInterface.ORIENTATION_FLIP_HORIZONTAL)
                || (orientation == ExifInterface.ORIENTATION_FLIP_VERTICAL))
            matrix.preScale(-1, 1);
        else if ((orientation == ExifInterface.ORIENTATION_TRANSPOSE)
                || (orientation == ExifInterface.ORIENTATION_TRANSVERSE))
            matrix.preScale(1, -1);

        return Bitmap.createBitmap(bitmap, 0, 0, bitmap.getWidth(), bitmap.getHeight(), matrix, true);
    }

    public static Bitmap extractRect(Bitmap bitmap, int px, int py, int rw, int rh) {
        if (bitmap == null)
            return null;

        int w = bitmap.getWidth();
        int h = bitmap.getHeight();
        if ((px != 0) || (py != 0) || (rw != w) || (rh != h)) {
            Bitmap.Config conf = Bitmap.Config.ARGB_8888;
            Bitmap scaled = Bitmap.createBitmap(rw, rh, conf);
            Canvas canvas = new Canvas(scaled);
            canvas.drawBitmap(bitmap, new Rect(px, py, px + rw, py + rh), new Rect(0, 0, rw, rh), null);
            bitmap = scaled;
        }
        return bitmap;
    }

    public static boolean saveBitmap(Bitmap bitmap, File file) {
        if (bitmap == null)
            return false;

        try (FileOutputStream stream = new FileOutputStream(file)) {
            return bitmap.compress(Bitmap.CompressFormat.JPEG, 85, stream);
        } catch (Exception e) {
            e.printStackTrace();
        }
        return false;
    }

    public int getBitmapDataSize(int w, int h, int px, int py, int rw, int rh) {
        if (bitmap == null)
            bitmap = getBitmap(srcPath, size, orientation, w, h);
        else
            bitmap = Bitmap.createScaledBitmap(bitmap, w, h, true);

        bitmap = extractRect(bitmap, px, py, rw, rh);
        if (bitmap == null)
            return 0;

        try {
            ByteArrayOutputStream stream = new ByteArrayOutputStream();
            if (!bitmap.compress(Bitmap.CompressFormat.JPEG, 85, stream))
                return 0;

            bitmapData = stream.toByteArray();
            return bitmapData.length;
        } catch (Exception e) {
        }
        return 0;
    }

    public boolean getBitmapData(byte[] buffer) {
        try {
            System.arraycopy(bitmapData, 0, buffer, 0, bitmapData.length);
            return true;
        } catch (Exception e) {
        }
        return false;
    }

    public void freeBitmap() {
        bitmap = null;
        bitmapData = null;
        size = null;
        srcPath = null;
        orientation = 0;
    }
}
