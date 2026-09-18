import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import org.json.JSONArray;
import org.json.JSONObject;

// Runs as ADB's shell user; no installed package, service or root access.
public final class QuestCatalog {
    public static void main(String[] args) throws Exception {
        Class<?> threadClass = Class.forName("android.app.ActivityThread");
        Object thread = threadClass.getMethod("systemMain").invoke(null);
        Context context = (Context) threadClass.getMethod("getSystemContext").invoke(thread);
        PackageManager pm = context.getPackageManager();
        JSONArray apps = new JSONArray();
        for (ApplicationInfo info : pm.getInstalledApplications(0)) {
            if ((info.flags & ApplicationInfo.FLAG_SYSTEM) != 0) continue;
            JSONObject app = new JSONObject();
            app.put("package", info.packageName);
            app.put("name", pm.getApplicationLabel(info).toString());
            apps.put(app);
        }
        System.out.println(apps.toString());
        System.exit(0);
    }
}
