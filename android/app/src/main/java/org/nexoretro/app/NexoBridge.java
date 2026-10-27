package org.nexoretro.app;

import android.app.Activity;
import android.app.UiModeManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;

import org.libsdl.app.SDL;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/**
 * Le pont entre le C++ de Nexo Retro et Android (voir src/android/Jni.cpp).
 *
 * Ce que le C++ fait avec Windows (WinHTTP, BCrypt, tar.exe...), il le
 * demande ici à Java, qui a tout cela dans le système : HTTPS, chiffrement,
 * fichiers .zip, accès aux fichiers. Toutes les méthodes sont statiques et
 * appelées depuis le C++, parfois depuis des threads d'arrière-plan.
 */
public final class NexoBridge {
    private static final String TAG = "NexoRetro";

    private NexoBridge() {}

    private static Context context() {
        return SDL.getContext();
    }

    // --- L'appareil ----------------------------------------------------------------------

    /** Version TV (BuildConfig), ou une télé qui fait tourner la version mobile. */
    public static boolean isTv() {
        if (BuildConfig.IS_TV)
            return true;
        UiModeManager ui = (UiModeManager) context().getSystemService(Context.UI_MODE_SERVICE);
        return ui != null && ui.getCurrentModeType() == Configuration.UI_MODE_TYPE_TELEVISION;
    }

    /** L'appli peut-elle lire et écrire dans tout le stockage (dossier NexoRetro) ? */
    public static boolean hasAllFilesAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
            return Environment.isExternalStorageManager();
        return context().checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED;
    }

    /** Ouvre la page du système où l'on autorise l'accès aux fichiers. */
    public static void requestAllFilesAccess() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + context().getPackageName()));
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                context().startActivity(intent);
            } else if (context() instanceof Activity) {
                ((Activity) context()).requestPermissions(new String[] {
                        android.Manifest.permission.READ_EXTERNAL_STORAGE,
                        android.Manifest.permission.WRITE_EXTERNAL_STORAGE}, 1);
            }
        } catch (Exception e) {
            Log.w(TAG, "Accès aux fichiers : " + e);
        }
    }

    /** Recopie les fichiers de l'APK (assets/) dans un dossier, sous-dossiers compris. */
    // Date d'installation de l'APK : change à chaque mise à jour (même sans
    // changer de numéro de version, comme pendant le développement).
    public static long apkStamp() {
        try {
            return context().getPackageManager().getPackageInfo(context().getPackageName(), 0).lastUpdateTime;
        } catch (Exception e) {
            return 0;
        }
    }

    public static boolean copyAssets(String dest) {
        try {
            copyAssetDir(context().getAssets(), "", new File(dest));
            return true;
        } catch (Exception e) {
            Log.w(TAG, "Recopie des fichiers de l'appli : " + e);
            return false;
        }
    }

    private static void copyAssetDir(AssetManager assets, String path, File dest) throws Exception {
        String[] names = assets.list(path);
        if (names == null || names.length == 0) {
            // Un fichier (un dossier vide n'existe pas dans un APK).
            dest.getParentFile().mkdirs();
            try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(dest)) {
                copy(in, out);
            }
            return;
        }
        dest.mkdirs();
        for (String name : names) {
            // (Dossiers ajoutés par Android lui-même dans tout APK.)
            if (path.isEmpty() && (name.equals("images") || name.equals("webkit") || name.equals("geoid_map")))
                continue;
            copyAssetDir(assets, path.isEmpty() ? name : path + "/" + name, new File(dest, name));
        }
    }

    // --- Téléchargement ----------------------------------------------------------------------

    /**
     * Télécharge une adresse (HTTPS compris). status[0] reçoit le code HTTP
     * (0 si pas de réponse). Renvoie le contenu, ou null.
     */
    public static byte[] httpGet(String url, String userAgent, int[] status) {
        status[0] = 0;
        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection) new URL(url).openConnection();
            connection.setRequestProperty("User-Agent", userAgent);
            connection.setConnectTimeout(15000);
            connection.setReadTimeout(30000);
            connection.setInstanceFollowRedirects(true);
            status[0] = connection.getResponseCode();
            InputStream in = status[0] >= 400 ? connection.getErrorStream() : connection.getInputStream();
            if (in == null)
                return new byte[0];
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            copy(in, out);
            in.close();
            return out.toByteArray();
        } catch (Exception e) {
            Log.w(TAG, "Téléchargement de " + url + " : " + e);
            return null;
        } finally {
            if (connection != null)
                connection.disconnect();
        }
    }

    // --- Chiffrement (relais du jeu en ligne) ---------------------------------------------------

    public static byte[] sha256(byte[] data) {
        try {
            return MessageDigest.getInstance("SHA-256").digest(data);
        } catch (Exception e) {
            return null;
        }
    }

    /**
     * AES-256-GCM : chiffre (texte chiffré suivi de l'étiquette de 16 octets),
     * ou déchiffre en vérifiant l'étiquette. null si le paquet est refusé.
     */
    public static byte[] aesGcm(boolean encrypt, byte[] key, byte[] nonce, byte[] data) {
        try {
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(encrypt ? Cipher.ENCRYPT_MODE : Cipher.DECRYPT_MODE, new SecretKeySpec(key, "AES"),
                    new GCMParameterSpec(128, nonce));
            return cipher.doFinal(data);
        } catch (Exception e) {
            return null;
        }
    }

    // --- Archives .zip ----------------------------------------------------------------------------

    public static boolean unzip(String zipPath, String destDir) {
        File dest = new File(destDir);
        try (ZipFile zip = new ZipFile(zipPath)) {
            String root = dest.getCanonicalPath() + File.separator;
            Enumeration<? extends ZipEntry> entries = zip.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                File out = new File(dest, entry.getName());
                // Un nom comme "../../x" ne doit pas sortir du dossier ("zip slip").
                if (!out.getCanonicalPath().startsWith(root))
                    continue;
                if (entry.isDirectory()) {
                    out.mkdirs();
                    continue;
                }
                out.getParentFile().mkdirs();
                try (InputStream in = zip.getInputStream(entry); OutputStream o = new FileOutputStream(out)) {
                    copy(in, o);
                }
            }
            return true;
        } catch (Exception e) {
            Log.w(TAG, "Décompression de " + zipPath + " : " + e);
            return false;
        }
    }

    public static String[] listZip(String zipPath) {
        ArrayList<String> names = new ArrayList<>();
        try (ZipFile zip = new ZipFile(zipPath)) {
            Enumeration<? extends ZipEntry> entries = zip.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                if (!entry.isDirectory())
                    names.add(entry.getName());
            }
        } catch (Exception e) {
            return null;
        }
        return names.toArray(new String[0]);
    }

    public static boolean zipFolder(String folder, String zipPath) {
        File root = new File(folder);
        try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(zipPath))) {
            addToZip(out, root, "");
            return true;
        } catch (Exception e) {
            Log.w(TAG, "Création de " + zipPath + " : " + e);
            return false;
        }
    }

    private static void addToZip(ZipOutputStream out, File file, String name) throws Exception {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children)
                addToZip(out, child, name.isEmpty() ? child.getName() : name + "/" + child.getName());
            return;
        }
        out.putNextEntry(new ZipEntry(name));
        try (InputStream in = new FileInputStream(file)) {
            copy(in, out);
        }
        out.closeEntry();
    }

    private static void copy(InputStream in, OutputStream out) throws Exception {
        byte[] buffer = new byte[65536];
        int n;
        while ((n = in.read(buffer)) > 0)
            out.write(buffer, 0, n);
    }
}
