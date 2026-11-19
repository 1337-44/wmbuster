package pl.wmbuster.nanocullogger;

import org.junit.Test;
import static org.junit.Assert.*;

public final class WmbusDecoderTest {
    @Test public void unencryptedVolumeRecord(){
        byte[] b=new byte[]{
            16,0x44,(byte)0x95,0x11,0x12,0x34,0x56,0x78,
            0x01,0x07,0x78,
            0x04,0x13,0x40,(byte)0xE2,0x01,0x00
        };
        WmbusDecoder.Result d=WmbusDecoder.decode(b);
        assertEquals("oms",d.status);
        assertEquals(1,d.records.size());
        assertEquals("Volume",d.records.get(0).get("name"));
        assertEquals("123.456",d.records.get(0).get("value"));
        assertEquals("m3",d.records.get(0).get("unit"));
    }
    @Test public void unknownProprietaryNeverPretendsToDecode(){
        byte[] b=new byte[]{12,0x44,0x01,0x02,0x11,0x22,0x33,0x44,1,1,
                             (byte)0xB6,0x01,0x13};
        WmbusDecoder.Result d=WmbusDecoder.decode(b);
        assertEquals("proprietary",d.status);
        assertTrue(d.records.isEmpty());
    }
    @Test public void encryptionDetected(){
        byte[] b=new byte[]{14,0x44,0x01,0x02,0x11,0x22,0x33,0x44,1,7,
                             0x7A,1,0,0,5};
        assertEquals("encrypted",WmbusDecoder.decode(b).status);
    }
}
