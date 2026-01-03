/*
 * VulkanActivity.java - NativeActivity wrapper for HinaVK
 *
 * This is a minimal wrapper that loads the native library and
 * provides an optional alert dialog for error messages.
 *
 * TODO: Change package name to match your example
 */
package com.hinavk.example;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.DialogInterface;
import android.content.pm.ApplicationInfo;
import android.os.Bundle;

import java.util.concurrent.Semaphore;

public class VulkanActivity extends NativeActivity {

    static {
        // Load native library (must match CMakeLists.txt target name)
        System.loadLibrary("main");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }

    // Modal dialog support (can be called from native code)
    private final Semaphore semaphore = new Semaphore(0, true);

    public void showAlert(final String message) {
        final VulkanActivity activity = this;

        ApplicationInfo applicationInfo = activity.getApplicationInfo();
        final String applicationName = applicationInfo.nonLocalizedLabel.toString();

        this.runOnUiThread(new Runnable() {
            public void run() {
                AlertDialog.Builder builder = new AlertDialog.Builder(activity, android.R.style.Theme_Material_Dialog_Alert);
                builder.setTitle(applicationName);
                builder.setMessage(message);
                builder.setPositiveButton("Close", new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int id) {
                        semaphore.release();
                    }
                });
                builder.setCancelable(false);
                AlertDialog dialog = builder.create();
                dialog.show();
            }
        });
        try {
            semaphore.acquire();
        } catch (InterruptedException e) {
        }
    }
}
