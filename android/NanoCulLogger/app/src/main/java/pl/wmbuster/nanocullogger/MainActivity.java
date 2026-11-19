package pl.wmbuster.nanocullogger;

import android.Manifest;
import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import com.hoho.android.usbserial.driver.UsbSerialDriver;
import com.hoho.android.usbserial.driver.UsbSerialProber;

import java.io.File;
import java.io.FileInputStream;
import java.io.OutputStream;
import java.util.Comparator;
import java.util.List;

/** Minimal offline UI. Start scanning from visible Activity to satisfy
 * Android 14+ location foreground-service launch restrictions. */
public final class MainActivity extends Activity {
    private static final int PERMISSIONS=8;
    private static final int EXPORT=9;
    private final String usbPermissionAction="pl.wmbuster.nanocullogger.USB_PERMISSION";
    private final Handler handler=new Handler(Looper.getMainLooper());
    private TextView statusText,gpsText,lastText,logText,usbText;
    private Spinner modeSpinner,baudSpinner;
    private UsbDevice selectedDevice;
    private boolean pendingStart;

    private final BroadcastReceiver usbPermissionReceiver=new BroadcastReceiver(){
        @Override public void onReceive(Context context,Intent intent) {
            if(!usbPermissionAction.equals(intent.getAction()))return;
            UsbManager usb=(UsbManager)getSystemService(USB_SERVICE);
            boolean granted=intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED,false);
            if(granted && selectedDevice!=null && usb.hasPermission(selectedDevice))
                launchService();
            else alert("Nie przyznano uprawnień USB dla NanoCUL");
        }
    };

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        IntentFilter filter=new IntentFilter(usbPermissionAction);
        if(Build.VERSION.SDK_INT>=33)
            registerReceiver(usbPermissionReceiver,filter,Context.RECEIVER_EXPORTED);
        else registerReceiver(usbPermissionReceiver,filter);

        ScrollView scroller=new ScrollView(this);
        LinearLayout root=new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad=(int)(20*getResources().getDisplayMetrics().density);
        root.setPadding(pad,pad,pad,pad);
        scroller.addView(root);
        setContentView(scroller);

        TextView title=text("NanoCUL GPS Logger",23,true);
        root.addView(title);
        root.addView(text("USB OTG / CH340 · wM-Bus · pozycja telefonu · JSONL",13,false));
        root.addView(text("Odbiornik USB (niewymagany Flipper Zero)",17,true));
        usbText=text("Wyszukiwanie urządzeń USB...",14,false);
        root.addView(usbText);
        root.addView(button("Odśwież USB",v->refreshUsb()));

        root.addView(text("Tryb nasłuchu",16,true));
        modeSpinner=new Spinner(this);
        modeSpinner.setAdapter(new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item,
                new String[]{"T+C","T1","C1","S1"}));
        root.addView(modeSpinner);
        root.addView(text("Prędkość portu szeregowego",16,true));
        baudSpinner=new Spinner(this);
        baudSpinner.setAdapter(new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item,
                new String[]{"38400","57600","115200"}));
        root.addView(baudSpinner);

        android.content.SharedPreferences prefs=getPreferences(MODE_PRIVATE);
        modeSpinner.setSelection(prefs.getInt("mode",0));
        baudSpinner.setSelection(prefs.getInt("baud",0));

        root.addView(button("▶ START — odbiór i zapis",v->begin()));
        root.addView(button("■ STOP",v->{
            startService(new Intent(this,NanoCulService.class)
                .setAction(NanoCulService.ACTION_STOP));
        }));
        root.addView(button("Eksportuj log JSONL",v->exportLog()));
        root.addView(text("Stan rejestratora",17,true));
        statusText=text("-",15,false);root.addView(statusText);
        root.addView(text("Lokalizacja telefonu",17,true));
        gpsText=text("-",14,false);root.addView(gpsText);
        root.addView(text("Ostatni telegram",17,true));
        lastText=text("-",14,false);root.addView(lastText);
        root.addView(text("Plik sesji",17,true));
        logText=text("-",12,false);root.addView(logText);
        root.addView(text("Log JSONL zawiera UTC, producenta, ID, RSSI, pola OMS, RAW oraz GPS lub null. " +
                "Pozycja oznacza lokalizację TELEFONU w chwili odbioru, nie licznika. " +
                "Nieudokumentowane i szyfrowane ramki pozostają oznaczone jako nierozkodowane.",12,false));

        handler.post(refreshLoop);
        refreshUsb();
    }
    private TextView text(String value,int size,boolean bold){
        TextView t=new TextView(this);
        t.setText(value);
        t.setTextSize(size);
        if(bold)t.setTypeface(null,android.graphics.Typeface.BOLD);
        t.setPadding(0,8,0,8);
        return t;
    }
    private Button button(String label,View.OnClickListener listener){
        Button b=new Button(this);b.setText(label);b.setOnClickListener(listener);return b;
    }
    private void alert(String s){Toast.makeText(this,s,Toast.LENGTH_LONG).show();}

    private final Runnable refreshLoop=new Runnable() {
        @Override public void run() {
            if(statusText!=null) {
                statusText.setText((NanoCulService.running?"● Rejestracja trwa · ":"")+
                        NanoCulService.status+"\nRamki: "+NanoCulService.received+
                        " · Błędne: "+NanoCulService.rejected);
                gpsText.setText(NanoCulService.gpsStatus);
                lastText.setText(NanoCulService.latest);
                logText.setText(NanoCulService.logFilePath.isEmpty()?
                    "(plik zostanie utworzony po starcie)":NanoCulService.logFilePath);
            }
            handler.postDelayed(this,1000);
        }
    };

    private List<UsbSerialDriver> drivers() {
        UsbManager usb=(UsbManager)getSystemService(USB_SERVICE);
        List<UsbSerialDriver> list=UsbSerialProber.getDefaultProber().findAllDrivers(usb);
        list.sort(Comparator.comparingInt(d ->
            d.getDevice().getVendorId()==0x1A86 ? 0:1));
        return list;
    }
    private void refreshUsb(){
        List<UsbSerialDriver> all=drivers();
        if(all.isEmpty()){
            usbText.setText("Nie wykryto konwertera USB-Serial. Podłącz NanoCUL przez USB OTG.");
            return;
        }
        UsbDevice d=all.get(0).getDevice();
        usbText.setText(String.format(java.util.Locale.ROOT,
            "Znaleziono USB-Serial VID:%04X PID:%04X (%d urządzeń)",
            d.getVendorId(),d.getProductId(),all.size()));
    }
    private boolean hasLocationPermission(){
        return checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)==
                   PackageManager.PERMISSION_GRANTED ||
               checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION)==
                   PackageManager.PERMISSION_GRANTED;
    }
    private void begin(){
        if(NanoCulService.running){alert("Rejestrator już pracuje");return;}
        List<UsbSerialDriver> d=drivers();
        if(d.isEmpty()){alert("Podłącz NanoCUL przez USB OTG");return;}
        selectedDevice=d.get(0).getDevice();
        pendingStart=true;
        if(!hasLocationPermission()){
            if(Build.VERSION.SDK_INT>=33) requestPermissions(new String[]{
                Manifest.permission.ACCESS_FINE_LOCATION,
                Manifest.permission.ACCESS_COARSE_LOCATION,
                Manifest.permission.POST_NOTIFICATIONS},PERMISSIONS);
            else requestPermissions(new String[]{
                Manifest.permission.ACCESS_FINE_LOCATION,
                Manifest.permission.ACCESS_COARSE_LOCATION},PERMISSIONS);
            return;
        }
        requestUsbIfNeeded();
    }
    @Override public void onRequestPermissionsResult(int request,String[] names,int[] result){
        super.onRequestPermissionsResult(request,names,result);
        if(request==PERMISSIONS && pendingStart) {
            if(hasLocationPermission())requestUsbIfNeeded();
            else {pendingStart=false;alert("Włącz dostęp do lokalizacji, aby rejestrować GPS");}
        }
    }
    private void requestUsbIfNeeded(){
        if(selectedDevice==null)return;
        UsbManager usb=(UsbManager)getSystemService(USB_SERVICE);
        if(usb.hasPermission(selectedDevice)){launchService();return;}
        Intent permission=new Intent(usbPermissionAction).setPackage(getPackageName());
        PendingIntent pi=PendingIntent.getBroadcast(this,11,permission,
                PendingIntent.FLAG_MUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        usb.requestPermission(selectedDevice,pi);
    }
    private void launchService(){
        if(!pendingStart || selectedDevice==null)return;
        pendingStart=false;
        int baud=Integer.parseInt(baudSpinner.getSelectedItem().toString());
        String mode=modeSpinner.getSelectedItem().toString();
        getPreferences(MODE_PRIVATE).edit()
                .putInt("mode",modeSpinner.getSelectedItemPosition())
                .putInt("baud",baudSpinner.getSelectedItemPosition()).apply();
        Intent intent=new Intent(this,NanoCulService.class)
                .setAction(NanoCulService.ACTION_START)
                .putExtra(NanoCulService.EXTRA_ID,selectedDevice.getDeviceId())
                .putExtra(NanoCulService.EXTRA_BAUD,baud)
                .putExtra(NanoCulService.EXTRA_MODE,mode);
        try{
            if(Build.VERSION.SDK_INT>=26)startForegroundService(intent);
            else startService(intent);
        }catch(Exception ex){alert("Nie można rozpocząć: "+ex.getMessage());}
    }
    private void exportLog(){
        String path=NanoCulService.logFilePath;
        if(path.isEmpty() || !new File(path).exists()){
            alert("Brak pliku sesji");return;
        }
        Intent save=new Intent(Intent.ACTION_CREATE_DOCUMENT);
        save.addCategory(Intent.CATEGORY_OPENABLE);
        save.setType("application/x-ndjson");
        save.putExtra(Intent.EXTRA_TITLE,new File(path).getName());
        startActivityForResult(save,EXPORT);
    }
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);
        if(request!=EXPORT || result!=RESULT_OK || data==null || data.getData()==null)return;
        try(FileInputStream input=new FileInputStream(NanoCulService.logFilePath);
            OutputStream output=getContentResolver().openOutputStream(data.getData())){
            if(output==null)throw new Exception("No output stream");
            byte[] buffer=new byte[8192];int n;
            while((n=input.read(buffer))>=0)output.write(buffer,0,n);
            output.flush();
            alert("Zapisano log JSONL");
        }catch(Exception ex){alert("Eksport nieudany: "+ex.getMessage());}
    }
    @Override protected void onDestroy(){
        handler.removeCallbacks(refreshLoop);
        unregisterReceiver(usbPermissionReceiver);
        super.onDestroy();
    }
}
