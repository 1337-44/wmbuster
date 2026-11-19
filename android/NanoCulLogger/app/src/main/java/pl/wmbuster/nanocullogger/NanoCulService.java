package pl.wmbuster.nanocullogger;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbManager;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Build;
import android.os.Environment;
import android.os.IBinder;
import android.os.Looper;
import android.os.SystemClock;

import com.hoho.android.usbserial.driver.UsbSerialDriver;
import com.hoho.android.usbserial.driver.UsbSerialPort;
import com.hoho.android.usbserial.driver.UsbSerialProber;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.List;

/**
 * Foreground USB Host + phone-position logger. Session runs while screen is off.
 * Only on-device files; no cloud account, background uploads or internet permission.
 */
public final class NanoCulService extends Service implements LocationListener {
    public static final String ACTION_START="pl.wmbuster.nanocullogger.START";
    public static final String ACTION_STOP="pl.wmbuster.nanocullogger.STOP";
    public static final String EXTRA_ID="usb_device_id";
    public static final String EXTRA_BAUD="baud";
    public static final String EXTRA_MODE="mode";
    public static volatile boolean running;
    public static volatile String status="Zatrzymano";
    public static volatile String latest="-";
    public static volatile String gpsStatus="Pozycja niedostępna";
    public static volatile String logFilePath="";
    public static volatile int received;
    public static volatile int rejected;
    private static final String CHANNEL="nanocul_capture";
    private volatile boolean stopRequested;
    private volatile Location latestLocation;
    private Thread reader;
    private UsbSerialPort port;
    private UsbDeviceConnection usbConnection;
    private LocationManager locationManager;
    private BufferedWriter writer;
    private final Object fileLock=new Object();
    private int baud;
    private String mode;

    @Override public IBinder onBind(Intent intent) { return null; }

    @Override public int onStartCommand(Intent intent,int flags,int startId) {
        if(intent!=null && ACTION_STOP.equals(intent.getAction())) {
            status="Zatrzymano na żądanie";
            stopRequested=true;
            stopSelf();
            return START_NOT_STICKY;
        }
        if(intent==null || !ACTION_START.equals(intent.getAction()))
            return START_NOT_STICKY;
        if(running) return START_NOT_STICKY;

        try {
            startInForeground();
        } catch(Exception ex) {
            status="Nie można uruchomić usługi: "+ex.getMessage();
            stopSelf();
            return START_NOT_STICKY;
        }
        int id=intent.getIntExtra(EXTRA_ID,-1);
        baud=intent.getIntExtra(EXTRA_BAUD,38400);
        mode=intent.getStringExtra(EXTRA_MODE);
        if(mode==null) mode="T+C";
        running=true;
        stopRequested=false;
        received=0;
        rejected=0;
        latest="-";
        logFilePath="";
        status="Otwieranie NanoCUL USB...";
        startPhoneLocation();
        reader=new Thread(()->capture(id),"nanocul-usb-reader");
        reader.start();
        return START_NOT_STICKY;
    }

    private void startInForeground() {
        NotificationManager nm=(NotificationManager)getSystemService(NOTIFICATION_SERVICE);
        nm.createNotificationChannel(new NotificationChannel(CHANNEL,"NanoCUL GPS — odbiór",
                NotificationManager.IMPORTANCE_LOW));
        Intent show=new Intent(this,MainActivity.class);
        PendingIntent tap=PendingIntent.getActivity(this,0,show,
                PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        Notification notification=new Notification.Builder(this,CHANNEL)
                .setSmallIcon(android.R.drawable.stat_notify_sync)
                .setContentTitle("NanoCUL GPS Logger")
                .setContentText("Odbiór wM-Bus przez USB i zapis pozycji telefonu")
                .setContentIntent(tap).setOngoing(true).build();
        if(Build.VERSION.SDK_INT>=29)
            startForeground(77,notification,ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION);
        else startForeground(77,notification);
    }

    private void startPhoneLocation() {
        if(checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)!=PackageManager.PERMISSION_GRANTED &&
           checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION)!=PackageManager.PERMISSION_GRANTED) {
            gpsStatus="Brak zgody na lokalizację";
            return;
        }
        locationManager=(LocationManager)getSystemService(Context.LOCATION_SERVICE);
        for(String provider:new String[]{LocationManager.GPS_PROVIDER,LocationManager.NETWORK_PROVIDER}) {
            try {
                if(locationManager.isProviderEnabled(provider)) {
                    locationManager.requestLocationUpdates(provider,1000L,0.0f,this,
                            Looper.getMainLooper());
                }
            } catch(IllegalArgumentException|SecurityException ex) {
                gpsStatus="Lokalizacja: "+ex.getMessage();
            }
        }
    }

    @Override public void onLocationChanged(Location location) {
        if(location==null || !location.hasAccuracy()) return;
        Location old=latestLocation;
        if(old==null || old.getElapsedRealtimeNanos()+15_000_000_000L <
                         SystemClock.elapsedRealtimeNanos() ||
           location.getAccuracy() <= old.getAccuracy()*2.0f) {
            latestLocation=new Location(location);
        }
        Location displayed=latestLocation;
        gpsStatus=String.format(java.util.Locale.ROOT,"GPS %.6f, %.6f ±%.0f m",
                displayed.getLatitude(),displayed.getLongitude(),displayed.getAccuracy());
    }

    private JSONObject currentLocation() throws Exception {
        Location location=latestLocation;
        if(location==null) return null;
        long age=(SystemClock.elapsedRealtimeNanos()-
                location.getElapsedRealtimeNanos())/1_000_000L;
        if(age<0 || age>20_000L) {
            gpsStatus="Pozycja nieaktualna (bez współrzędnych w logu)";
            return null;
        }
        JSONObject json=new JSONObject();
        json.put("source","phone");
        json.put("lat",location.getLatitude());
        json.put("lon",location.getLongitude());
        json.put("accuracy_m",location.hasAccuracy()?location.getAccuracy():JSONObject.NULL);
        json.put("altitude_m",location.hasAltitude()?location.getAltitude():JSONObject.NULL);
        json.put("provider",location.getProvider());
        json.put("fix_age_ms",age);
        json.put("fix_time_utc",Instant.ofEpochMilli(location.getTime()).toString());
        return json;
    }

    private void createLog() throws Exception {
        File directory=getExternalFilesDir(Environment.DIRECTORY_DOCUMENTS);
        if(directory==null)directory=getFilesDir();
        File logDir=new File(directory,"nanoCUL");
        if(!logDir.exists() && !logDir.mkdirs()) throw new Exception("Cannot create logs dir");
        String stamp=DateTimeFormatter.ofPattern("yyyyMMdd_HHmmss",java.util.Locale.ROOT)
                .withZone(ZoneOffset.UTC).format(Instant.now());
        File f=new File(logDir,"nanocul_"+stamp+".jsonl");
        synchronized(fileLock) {
            writer=new BufferedWriter(new OutputStreamWriter(
                    new FileOutputStream(f,false),StandardCharsets.UTF_8));
            logFilePath=f.getAbsolutePath();
        }
    }

    private void writeLog(JSONObject entry) {
        synchronized(fileLock) {
            if(writer==null)return;
            try {
                writer.write(entry.toString());
                writer.newLine();
                writer.flush(); // retain captured data after sudden disconnect/power loss
            } catch(Exception e) {
                status="Błąd zapisu na dysku: "+e.getMessage();
            }
        }
    }

    private void logFrame(CulParser.Frame frame) {
        try {
            WmbusDecoder.Result d=WmbusDecoder.decode(frame.linkFrame);
            JSONObject json=new JSONObject();
            json.put("timestamp_utc",Instant.now().toString());
            json.put("source","nanoCUL_USB");
            json.put("mode",mode);
            json.put("format",frame.format);
            json.put("rssi_dbm",frame.rssiDbm==null?JSONObject.NULL:frame.rssiDbm);
            json.put("wire_hex",frame.wireHex);
            json.put("frame_hex",CulParser.toHex(frame.linkFrame));
            json.put("manufacturer",d.manufacturer);
            json.put("id",d.id);
            json.put("id_hex",d.idHex);
            json.put("version",d.version);
            json.put("media",d.media);
            json.put("medium_code",d.medium);
            json.put("ci",d.ci);
            json.put("decode_status",d.status);
            json.put("summary",d.summary);
            JSONArray values=new JSONArray();
            for(java.util.Map<String,String> record:d.records)
                values.put(new JSONObject(record));
            json.put("records",values);
            JSONObject location=currentLocation();
            json.put("location",location==null?JSONObject.NULL:location);
            writeLog(json);
            received++;
            latest=d.manufacturer+" "+d.id+" · "+d.summary;
            status="Odbiór: "+received+" ramek (odrzucone: "+rejected+")";
        } catch(Exception error) {
            rejected++;
            status="Błąd parsera: "+error.getMessage();
        }
    }

    private void logInvalid(String text) {
        rejected++;
        try {
            JSONObject json=new JSONObject();
            json.put("timestamp_utc",Instant.now().toString());
            json.put("source","nanoCUL_USB");
            json.put("decode_status","invalid_crc_or_frame");
            json.put("cul_line",text);
            JSONObject loc=currentLocation();
            json.put("location",loc==null?JSONObject.NULL:loc);
            writeLog(json);
        } catch(Exception ignored) {}
        status="Odbiór: "+received+" ramek (odrzucone: "+rejected+")";
    }

    private void writeCommand(String command) throws Exception {
        port.write(command.getBytes(StandardCharsets.US_ASCII),2000);
    }

    private void capture(int usbId) {
        try {
            UsbManager manager=(UsbManager)getSystemService(Context.USB_SERVICE);
            UsbSerialDriver chosen=null;
            List<UsbSerialDriver> drivers=UsbSerialProber.getDefaultProber().findAllDrivers(manager);
            for(UsbSerialDriver driver:drivers)
                if(driver.getDevice().getDeviceId()==usbId) {chosen=driver;break;}
            if(chosen==null)throw new Exception("Nie znaleziono portu USB-Serial");
            UsbDevice device=chosen.getDevice();
            if(!manager.hasPermission(device)) throw new Exception("Brak zgody na urządzenie USB");
            usbConnection=manager.openDevice(device);
            if(usbConnection==null)throw new Exception("UsbManager.openDevice odmówił");
            port=chosen.getPorts().get(0);
            port.open(usbConnection);
            port.setParameters(baud,8,UsbSerialPort.STOPBITS_1,UsbSerialPort.PARITY_NONE);
            try {port.setDTR(false);}catch(Exception ignored){}
            createLog();
            status="USB połączone. Inicjalizowanie culfw...";
            Thread.sleep(1200); // old Nano bootloader can reset on opening serial
            writeCommand("V\n\r");
            Thread.sleep(100);
            writeCommand(mode.equals("S1")?"brs\n\r":
                         mode.equals("T1")?"brt\n\r":"brc\n\r");
            Thread.sleep(100);
            writeCommand("X21\n\r");

            CulParser parser=new CulParser(new CulParser.Listener(){
                @Override public void onFrame(CulParser.Frame f){logFrame(f);}
                @Override public void onText(String text){
                    if(text.startsWith("V ") || text.endsWith("MODE")) {
                        latest="nanoCUL: "+text;
                        status="Nasłuch USB "+mode+" / "+baud+" baud";
                    } else if(text.contains("not supported")||text.startsWith("? "))
                        status="Odpowiedź NanoCUL: "+text;
                }
                @Override public void onInvalid(String text){logInvalid(text);}
            });
            byte[] chunk=new byte[512];
            status="Nasłuch USB "+mode+" / "+baud+" baud";
            while(!stopRequested && !Thread.currentThread().isInterrupted()) {
                int n=port.read(chunk,200);
                if(n>0)parser.feed(chunk,n);
            }
        } catch(Exception ex) {
            if(!stopRequested)status="Błąd USB: "+ex.getMessage();
        } finally {
            stopRequested=true;
            closeResources();
            running=false;
            stopSelf();
        }
    }

    private void closeResources() {
        synchronized(fileLock) {
            if(writer!=null) {
                try{writer.close();}catch(Exception ignored){}
                writer=null;
            }
        }
        if(port!=null){
            try{port.close();}catch(Exception ignored){}
            port=null;
        }
        if(usbConnection!=null){
            try{usbConnection.close();}catch(Exception ignored){}
            usbConnection=null;
        }
    }

    @Override public void onDestroy() {
        stopRequested=true;
        try {
            if(locationManager!=null)locationManager.removeUpdates(this);
        } catch(Exception ignored){}
        if(reader!=null)reader.interrupt();
        closeResources();
        running=false;
        super.onDestroy();
    }
}
