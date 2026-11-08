package ai.mofa.honorautoroot;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

public final class BootReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        if (intent == null || !Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) return;
        if (!context.getSharedPreferences("settings", Context.MODE_PRIVATE)
                .getBoolean("auto_root", false)) return;
        Intent service = new Intent(context, RootService.class).setAction(RootService.ACTION_RUN);
        context.startForegroundService(service);
    }
}
