package pl.wmbuster.nanocullogger;

import java.io.ByteArrayOutputStream;
import java.util.Arrays;

/**
 * Stateful ASCII line accumulator and wM-Bus Format A/B CRC decoder.
 * nanoCUL culfw output: b<hex Format A>[LQI RSSI] or bY<hex Format B>[LQI RSSI].
 * CRCs validated and removed before handing the frame to wM-Bus decoder.
 * No decryption or unsupported proprietary payload guessing.
 */
public final class CulParser {
    public static final class Frame {
        public final byte[] linkFrame;
        public final String wireHex;
        public final String format;
        public final Integer rssiDbm;
        Frame(byte[] linkFrame, String wireHex, String format, Integer rssiDbm) {
            this.linkFrame = linkFrame;
            this.wireHex = wireHex;
            this.format = format;
            this.rssiDbm = rssiDbm;
        }
    }
    public interface Listener {
        void onFrame(Frame frame);
        void onText(String line);
        void onInvalid(String line);
    }
    private final Listener listener;
    private final ByteArrayOutputStream line = new ByteArrayOutputStream(650);
    private boolean dropping;

    public CulParser(Listener listener) {
        this.listener = listener;
    }
    public void feed(byte[] bytes, int length) {
        for(int i = 0; i < length; i++) {
            int b = bytes[i] & 0xff;
            if(b == 13 || b == 10) {
                if(!dropping && line.size() > 0) process(line.toString());
                line.reset();
                dropping = false;
            } else if(!dropping && b >= 32 && b < 127) {
                if(line.size() < 640) line.write(b);
                else { dropping = true; line.reset(); }
            } else if(b < 32 || b >= 127) {
                dropping = true;
                line.reset();
            }
        }
    }
    private void process(String text) {
        if(!text.startsWith("b")) {
            listener.onText(text);
            return;
        }
        try {
            Frame decoded = parse(text);
            if(decoded == null) listener.onText(text);
            else listener.onFrame(decoded);
        } catch(IllegalArgumentException ex) {
            listener.onInvalid(text);
        }
    }
    public static Frame parse(String text) {
        boolean fmtB = text.startsWith("bY");
        int prefix = fmtB ? 2 : 1;
        if(text.length() <= prefix || text.charAt(0) != 'b' || hex(text.charAt(prefix)) < 0)
            return null;
        String value = text.substring(prefix);
        if((value.length() & 1) != 0 || value.length() < 24 || value.length() > 600)
            throw new IllegalArgumentException("bad CUL length");
        byte[] raw = new byte[value.length()/2];
        for(int i=0;i<raw.length;i++){
            int a=hex(value.charAt(i*2)), b=hex(value.charAt(i*2+1));
            if(a<0 || b<0) throw new IllegalArgumentException("nonhex");
            raw[i]=(byte)((a<<4)|b);
        }
        int L=raw[0]&0xff;
        if(L<10) throw new IllegalArgumentException("short link frame");
        int exp=fmtB?L+1:L+1+2*(1+(L-9+15)/16);
        if(exp>raw.length || (raw.length!=exp && raw.length!=exp+2))
            throw new IllegalArgumentException("truncated or extra CUL bytes");
        Integer rssi=null;
        if(raw.length==exp+2) {
            int signed=raw[exp+1]; // signed CC1101 RSSI register
            rssi=Math.max(-127,Math.min(127,signed/2-74));
        }
        byte[] payload;
        if(fmtB) {
            if(exp<13 || crc(raw,0,exp-2)!=(((raw[exp-2]&255)<<8)|(raw[exp-1]&255)))
                throw new IllegalArgumentException("CRC B");
            payload=Arrays.copyOf(raw,exp-2);
            payload[0]=(byte)(payload.length-1); // B L included CRC before stripping
        } else {
            int pos=0;
            ByteArrayOutputStream dst=new ByteArrayOutputStream();
            for(int blk=10;pos<exp;blk=16) {
                int size=pos==0?10:Math.min(16,exp-pos-2);
                if(size<=0 || pos+size+2>exp ||
                   crc(raw,pos,size)!=(((raw[pos+size]&255)<<8)|(raw[pos+size+1]&255)))
                    throw new IllegalArgumentException("CRC A");
                dst.write(raw,pos,size);
                pos+=size+2;
            }
            payload=dst.toByteArray();
        }
        if(payload.length<11 || payload.length!=((payload[0]&255)+1))
            throw new IllegalArgumentException("bad link L");
        return new Frame(payload,toHex(Arrays.copyOf(raw,exp)),fmtB?"B":"A",rssi);
    }
    private static int crc(byte[] a,int offset,int len) {
        int c=0;
        for(int i=offset;i<offset+len;i++){
            c^=(a[i]&255)<<8;
            for(int j=0;j<8;j++) c=((c&0x8000)!=0)?((c<<1)^0x3d65)&0xffff:(c<<1)&0xffff;
        }
        return c^0xffff;
    }
    private static int hex(char c) {
        if(c>='0' && c<='9') return c-'0';
        if(c>='a' && c<='f') return c-'a'+10;
        if(c>='A' && c<='F') return c-'A'+10;
        return -1;
    }
    public static String toHex(byte[] a){
        StringBuilder sb=new StringBuilder(a.length*2);
        for(byte b:a){
            final String digs="0123456789ABCDEF";
            sb.append(digs.charAt((b>>4)&15)).append(digs.charAt(b&15));
        }
        return sb.toString();
    }
}
