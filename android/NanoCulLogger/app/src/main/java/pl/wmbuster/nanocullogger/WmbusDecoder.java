package pl.wmbuster.nanocullogger;

import java.math.BigDecimal;
import java.math.BigInteger;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Conservative EN13757/OMS subset decoder, not a vendor-specific decoder.
 * Never claim that opaque, encrypted, unknown CI or incomplete fields contain
 * a valid measurement. Preserve all raw bytes in the enclosing CUL frame.
 */
public final class WmbusDecoder {
    public static final class Result {
        public final String manufacturer, id, idHex, media;
        public final int version, medium, ci;
        public final String status, summary;
        public final List<Map<String,String>> records;
        Result(String manufacturer,String id,String idHex,String media,
               int version,int medium,int ci,String status,String summary,
               List<Map<String,String>> records) {
            this.manufacturer=manufacturer;this.id=id;this.idHex=idHex;this.media=media;
            this.version=version;this.medium=medium;this.ci=ci;
            this.status=status;this.summary=summary;this.records=records;
        }
    }

    private static int ub(byte[] a,int i){return a[i]&0xff;}
    private static String manufacturer(int m) {
        StringBuilder out=new StringBuilder();
        for(int shift: new int[]{10,5,0}) {
            int v=(m>>shift)&31;
            out.append(v>=1 && v<=26 ? (char)('A'+v-1) : '?');
        }
        return out.toString();
    }
    private static String medium(int type) {
        switch(type){
            case 1:return "Oil";
            case 2:return "Electricity";
            case 3:return "Gas";
            case 4:return "Heat";
            case 6:return "Hot water";
            case 7:return "Water";
            case 8:return "Heat cost allocator";
            case 10:return "Cooling";
            case 12:return "Heat return";
            case 22:return "Cold water";
            case 55:return "Radio converter";
            case 0x62:return "Warm water";
            case 0x72:return "Cold water";
            default:return String.format(java.util.Locale.ROOT,"Media 0x%02X",type);
        }
    }
    private static String deviceId(byte[] b) {
        StringBuilder id=new StringBuilder(8);
        for(int i=7;i>=4;i--) {
            int n=ub(b,i);
            if((n&15)>9 || (n>>4)>9) return hexId(b);
            id.append(Character.forDigit((n>>4)&15,10)).append(Character.forDigit(n&15,10));
        }
        return id.toString();
    }
    private static String hexId(byte[] b) {
        byte[] id=new byte[]{b[7],b[6],b[5],b[4]};
        return CulParser.toHex(id);
    }
    private static Result result(byte[] a,int med,int ci,String status,
                                 String summary,List<Map<String,String>> rec){
        int m=ub(a,2)|(ub(a,3)<<8);
        return new Result(manufacturer(m),deviceId(a),hexId(a),medium(med),
                          ub(a,8),med,ci,status,summary,rec);
    }
    private static Map<String,String> record(String label,String value,String unit,String raw) {
        Map<String,String> m=new LinkedHashMap<>();
        m.put("name",label);m.put("value",value);m.put("unit",unit);
        m.put("raw",raw);
        return m;
    }
    private static final class Vif {
        final String name,unit;final int exp;
        Vif(String n,String u,int e){name=n;unit=u;exp=e;}
    }
    private static Vif vif(int v){
        v&=127;
        if(v<=7) return new Vif("Energy","Wh",(v&7)-3);
        if(v<=15) return new Vif("Energy","kJ",(v&7)-3);
        if(v<=23) return new Vif("Volume","m3",(v&7)-6);
        if(v<=31) return new Vif("Mass","kg",(v&7)-3);
        if(v>=0x28 && v<=0x2F) return new Vif("Power","W",(v&7)-3);
        if(v>=0x30 && v<=0x37) return new Vif("Power","kJ/h",(v&7)-3);
        if(v>=0x38 && v<=0x3F) return new Vif("Flow","m3/h",(v&7)-6);
        if(v>=0x40 && v<=0x47) return new Vif("Flow","m3/min",(v&7)-7);
        if(v>=0x48 && v<=0x4F) return new Vif("Flow","m3/s",(v&7)-9);
        if(v>=0x50 && v<=0x57) return new Vif("Mass flow","kg/h",(v&7)-3);
        if(v>=0x58 && v<=0x5B) return new Vif("Flow temp","C",(v&3)-3);
        if(v>=0x5C && v<=0x5F) return new Vif("Return temp","C",(v&3)-3);
        if(v>=0x60 && v<=0x63) return new Vif("Temp diff","K",(v&3)-3);
        if(v>=0x64 && v<=0x67) return new Vif("External temp","C",(v&3)-3);
        if(v>=0x68 && v<=0x6B) return new Vif("Pressure","bar",(v&3)-3);
        if(v==0x6C) return new Vif("Date","",0);
        if(v==0x6D) return new Vif("Date/time","",0);
        if(v==0x6E) return new Vif("HCA","unit",0);
        return null;
    }
    private static int sizeOf(int dif){
        switch(dif&15) {
            case 0:case 8:case 15:return 0;
            case 1:case 9:return 1;
            case 2:case 10:return 2;
            case 3:case 11:return 3;
            case 4:case 5:case 12:return 4;
            case 6:case 14:return 6;
            case 7:return 8;
            case 13:return -1;
            default:return -2;
        }
    }
    private static boolean isBcd(int dif){
        int d=dif&15;
        return d==9 || d==10 || d==11 || d==12 || d==14;
    }
    private static BigDecimal readDecimal(byte[] bytes,int pos,int len,int dif) {
        if(isBcd(dif)) {
            StringBuilder digits=new StringBuilder();
            boolean negative=false;
            for(int i=pos+len-1;i>=pos;i--) {
                int upper=(ub(bytes,i)>>4)&15,lower=ub(bytes,i)&15;
                if(i==pos+len-1 && upper==15) {negative=true;upper=0;}
                if(upper>9 || lower>9) return null;
                digits.append(upper).append(lower);
            }
            BigDecimal v=new BigDecimal(digits.toString());
            return negative?v.negate():v;
        }
        if((dif&15)==5) {
            long bits=0;
            for(int i=0;i<4;i++) bits|=(long)ub(bytes,pos+i)<<(8*i);
            float fl=Float.intBitsToFloat((int)bits);
            return Float.isFinite(fl)?BigDecimal.valueOf(fl):null;
        }
        byte[] reversed=new byte[len];
        for(int i=0;i<len;i++) reversed[len-1-i]=bytes[pos+i];
        return new BigDecimal(new BigInteger(reversed));
    }
    private static String formatDate(byte[] a,int p,int len,boolean dateTime){
        if(len < (dateTime?4:2)) return null;
        // EN13757 type G (2-byte date) or type F (4-byte date/time).
        // Type F stores minute/hour in bytes 0/1, date bits in bytes 2/3.
        int o=dateTime?2:0;
        int d0=ub(a,p+o),d1=ub(a,p+o+1);
        int year=2000+((d0>>5)&7)+(((d1>>4)&15)<<3);
        int month=d1&15,day=d0&31;
        if(month<1 || month>12 || day<1 || day>31) return null;
        if(!dateTime) return String.format(java.util.Locale.ROOT,"%04d-%02d-%02d",year,month,day);
        int minute=ub(a,p)&63;
        int hour=ub(a,p+1)&31;
        if(minute>59 || hour>23) return null;
        return String.format(java.util.Locale.ROOT,"%04d-%02d-%02d %02d:%02d",year,month,day,hour,minute);
    }
    public static Result decode(byte[] a) {
        if(a==null || a.length<11 || (a[0]&255)+1!=a.length)
            throw new IllegalArgumentException("invalid stripped wM-Bus frame");
        int med=ub(a,9),ci=ub(a,10);
        List<Map<String,String>> records=new ArrayList<>();
        int p=11;
        if(ci==0x7A || ci==0x5A) {
            if(a.length<p+4) return result(a,med,ci,"truncated","Short application header",records);
            int cfg=ub(a,p+2)|(ub(a,p+3)<<8);
            int mode=(cfg>>8)&31;
            if(mode!=0) return result(a,med,ci,"encrypted","Encryption mode "+mode+" (key needed)",records);
            p+=4;
        } else if(ci==0x72 || ci==0x53 || ci==0x8B) {
            if(a.length<p+10) return result(a,med,ci,"truncated","Long application header",records);
            int cfg=ub(a,p+8)|(ub(a,p+9)<<8);
            int mode=(cfg>>8)&31;
            if(mode!=0) return result(a,med,ci,"encrypted","Encryption mode "+mode+" (key needed)",records);
            p+=10;
        } else if(ci==0x78 || ci==0x69 || ci==0x6B || ci==0x6F || ci==0x73) {
            // Plaintext application data.
        } else {
            return result(a,med,ci,ci>=0xA0 && ci<=0xB7?"proprietary":"unsupported",
                    String.format(java.util.Locale.ROOT,"CI=0x%02X; RAW saved",ci),records);
        }

        boolean partial=false;
        int limit=0;
        while(p<a.length && limit++<60) {
            int dif=ub(a,p++);
            if(dif==0x2F || dif==0x00 || dif==0x08) continue;
            if(dif==0x0F || dif==0x1F) {partial=true;break;}
            int extra=0;
            int old=dif;
            while((old&0x80)!=0) {
                if(p>=a.length || extra++>=10) {partial=true;break;}
                old=ub(a,p++);
            }
            if(partial || p>=a.length) {partial=true;break;}
            int vif=ub(a,p++);
            Vif meaning=vif(vif);
            int prev=vif;
            if((vif&127)==0x7B || (vif&127)==0x7D){
                if(p>=a.length) {partial=true;break;}
                prev=ub(a,p++);
                meaning=null; // extension table not yet implemented
            } else if((vif&127)==0x7C){
                if(p>=a.length) {partial=true;break;}
                int n=ub(a,p++);
                if(n>100 || p+n>a.length) {partial=true;break;}
                p+=n;
                meaning=null;
                prev=0;
            }
            int count=0;
            while((prev&0x80)!=0) {
                if(p>=a.length || count++>10) {partial=true;break;}
                prev=ub(a,p++);
            }
            if(partial)break;
            int n=sizeOf(dif);
            if(n==-1) {
                if(p>=a.length) {partial=true;break;}
                int lvar=ub(a,p++);
                if(lvar>191) {partial=true;break;} // not all special LVAR forms implemented
                n=lvar;
            } else if(n<0) {partial=true;break;}
            if(p+n>a.length) {partial=true;break;}
            if(n==0) continue;
            byte[] field=new byte[n];
            System.arraycopy(a,p,field,0,n);
            if(meaning!=null) {
                String val=null;
                if((vif&127)==0x6C) val=formatDate(a,p,n,false);
                else if((vif&127)==0x6D) val=formatDate(a,p,n,true);
                else if((dif&15)!=13) {
                    BigDecimal d=readDecimal(a,p,n,dif);
                    if(d!=null) val=d.scaleByPowerOfTen(meaning.exp).stripTrailingZeros().toPlainString();
                }
                if(val!=null) records.add(record(meaning.name,val,meaning.unit,CulParser.toHex(field)));
            }
            p+=n;
        }
        if(limit>=60) partial=true;
        String status=records.isEmpty()?"unsupported":(partial?"partial":"oms");
        String summary=records.isEmpty()?"No supported OMS values":
                records.get(0).get("name")+" "+records.get(0).get("value")+" "+records.get(0).get("unit");
        if(partial)summary+=" (partial)";
        return result(a,med,ci,status,summary,records);
    }
}
