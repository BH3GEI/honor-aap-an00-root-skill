package ai.mofa.honorautoroot;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.os.Process;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.text.DateFormat;
import java.util.Date;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

public final class RootService extends Service {
    static final String ACTION_CHECK = "ai.mofa.honorautoroot.CHECK_ROOT";
    static final String ACTION_RUN = "ai.mofa.honorautoroot.RUN_ROOT";
    private final AtomicBoolean running = new AtomicBoolean(false);
    private ExecutorService executor;

    @Override public void onCreate() {
        super.onCreate();
        executor = Executors.newSingleThreadExecutor();
        NotificationChannel channel = new NotificationChannel(
                "root", "Root status", NotificationManager.IMPORTANCE_LOW);
        getSystemService(NotificationManager.class).createNotificationChannel(channel);
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        Notification notification = new Notification.Builder(this, "root")
                .setContentTitle("Honor AutoRoot")
                .setContentText("Checking root state")
                .setSmallIcon(android.R.drawable.ic_lock_lock)
                .build();
        startForeground(1, notification);
        final String action = intent == null ? ACTION_CHECK : intent.getAction();
        if (!running.compareAndSet(false, true)) return START_NOT_STICKY;
        executor.execute(new Runnable() {
            @Override public void run() {
                String result;
                if (RootController.isRootAvailable()) {
                    result = "root already active; exploit not run";
                } else if (ACTION_RUN.equals(action)) {
                    result = RootController.runRoot(RootService.this);
                } else {
                    result = "root not available; no exploit requested";
                }
                writeStatus(result);
                running.set(false);
                stopSelf();
            }
        });
        return START_NOT_STICKY;
    }

    private void writeStatus(String result) {
        Log.i("HonorAutoRoot", "status: " + result.replace('\n', ' '));
        String text = DateFormat.getDateTimeInstance().format(new Date())
                + "\n" + result + "\napp uid=" + Process.myUid() + "\n";
        try (FileOutputStream output = new FileOutputStream(new File(getFilesDir(), "status.txt"))) {
            output.write(text.getBytes(StandardCharsets.UTF_8));
        } catch (Exception ignored) {}
    }

    @Override public IBinder onBind(Intent intent) { return null; }

    @Override public void onDestroy() {
        executor.shutdown();
        super.onDestroy();
    }
}
