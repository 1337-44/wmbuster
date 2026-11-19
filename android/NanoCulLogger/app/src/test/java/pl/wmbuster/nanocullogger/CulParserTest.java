package pl.wmbuster.nanocullogger;

import org.junit.Test;
import static org.junit.Assert.*;
import java.util.concurrent.atomic.AtomicInteger;

public final class CulParserTest {
    private static final String A =
        "b2944A511780735321315931CA1111100135D114581B59070615F689AD5"+
        "5EF517FDA5101C5464C0FD0B93A08FF474D8C18315";
    private static final String B =
        "bY3444372C964001233A168D20B96060C42109CFA2C3EE1DF1B12BE6D"+
        "E28F6986B9AA8625209FBD47B10CC7B1481EDD00D5D1B25E15C8625";

    @Test public void formatA() {
        CulParser.Frame f=CulParser.parse(A);
        assertEquals("A",f.format);
        assertEquals(42,f.linkFrame.length);
        assertEquals(41,f.linkFrame[0]&255);
        assertNotNull(f.rssiDbm);
    }
    @Test public void formatB() {
        CulParser.Frame f=CulParser.parse(B);
        assertEquals("B",f.format);
        assertEquals(51,f.linkFrame.length);
        assertEquals(50,f.linkFrame[0]&255);
    }
    @Test public void fragmentedUsbPackets() {
        AtomicInteger count=new AtomicInteger();
        CulParser p=new CulParser(new CulParser.Listener() {
            public void onFrame(CulParser.Frame f){count.incrementAndGet();}
            public void onInvalid(String l){fail("Unexpected invalid frame");}
            public void onText(String l){}
        });
        byte[] line=(A+"\r\n").getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        for(int i=0;i<line.length;i++)p.feed(new byte[]{line[i]},1);
        assertEquals(1,count.get());
    }
    @Test public void badCrcRejected() {
        String corrupt=A.substring(0,20)+"FF"+A.substring(22);
        try{CulParser.parse(corrupt);fail("bad CRC accepted");}
        catch(IllegalArgumentException expected){}
    }
}
