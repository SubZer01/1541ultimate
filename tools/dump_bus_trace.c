#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <stdint.h>

struct data_entry {
    uint16_t addr;
    uint8_t data;
    uint8_t flags;
};


static char *bin(uint64_t val, int bits, char *buffer)
{
  int bit;
  int leading = 1;
  int i = 0;
  while (--bits >= 0)
    {
      bit = ((val & (1LL << bits)) != 0LL);
      if (leading && (bits != 0) && ! bit)
	continue;
      leading = 0;
      buffer[i++] = '0' + bit;
    }
  buffer[i] = 0;
  return buffer;
}

void dump_trace(FILE *fi, int max, int text_mode, int trigger)
{
    uint32_t time = 0;
    int   r,i,z;
    
    struct data_entry d;
    struct data_entry prev = { 0x1234, 0x56, 0x00 };

    const char vcd_header[] = "$timescale\n 500 ns\n$end\n\n";
    const char vcd_middle[] = "\n$enddefinitions $end\n\n#0\n$dumpvars\n";

    //   ev_data_c <= sub & task & ev_data;
    //    vector_in <= phi2 & gamen & exromn & ba & interrupt & rom & io & rwn & data & addr;
    //
    // vector_in <= phi2 & dman & exromn & ba & irqn & rom & nmin & rwn & data & addr;
    // const char *labels[8] = { "RWn","NMIn","ROMn", "IRQn","BA","EXROMn","SYNC","PHI2" };
    const char *labels[8] = { "RWn","NMIn","ROMn", "IRQn","BA","DMAn","SYNC","PHI2" };
    const char *vic_regs[47] = { "M0X", "M0Y", "M1X", "M1Y", "M2X", "M2Y", "M3X", "M3Y",
            "M4X", "M4Y", "M5X", "M5Y", "M6X", "M6Y", "M7X", "M7Y",
            "MX8th", "D011", "D012", "LPX", "LPY", "SPREN", "D016", "SPEXPY", "MEMPNT", "IRQREG", "IRQEN",
            "SPPRI", "SPMC", "SPEXPX", "SSCOL", "SGCOL", "D020", "D021", "D022", "D023", "D024", "MM0", "MM1",
            "M0COL", "M1COL", "M2COL", "M3COL", "M4COL", "M5COL", "M6COL", "M7COL" };

    uint8_t   b,fla;
        
    z = 4;

    if (!text_mode) {
        printf(vcd_header);
        printf("$var wire 16 ! addr $end\n");
        printf("$var wire 8 # data $end\n");
        printf("$var wire 9 $ line $end\n");
        printf("$var wire 6 * cycle $end\n");
        for(b=0;b<8;b++) {
            if(*labels[b])
                printf("$var wire 1 %c %s $end\n", 48+b, labels[b]);
        }
        for(b=0;b<47;b++) {
            printf("$var wire 8 %c _%s $end\n", 56+b, vic_regs[b]);
        }
        printf(vcd_middle);
    } else {
        printf("ADDR,DATA,");
        for(b=0;b<8;b++) {
            printf("%s,", labels[b]);
        }
        printf("ADDR,DATA,");
        for(b=0;b<8;b++) {
            printf("%s,", labels[b]);
        }
        printf("Cycle,Line,Cycle\n");
    }
    char buffer[32];
    int cycle = 0;
    int enable = 0;

    for(i=0;i<max;i++) {
        r = fread(&d, z, 1, fi);
        if(r != 1)
            break;

        if ((d.flags & 0x80) == 0) {
            if (d.flags & 0x40) {
                enable = 1;
                cycle = 16028;
            } else {
                cycle ++;
            }
            cycle = cycle % 19656;
        }

        if (!enable) {
            i--;
            continue;
        }

        time ++;

        if (trigger >= 0) {
            if (((d.flags & 0x80) != 0) && (d.addr == trigger)) {
                trigger = -1;
            } else {
                i--;
                continue;
            }
        }

        if (text_mode) {
            printf("\"%04x\",\"%02x\",", d.addr, d.data);
            for(fla=d.flags,b=0;b<8;b++) {
                printf("%d,", fla & 1);
                fla >>= 1;
            }
            if (d.flags & 0x80) {
                printf("%d,%d,%d\n", cycle, cycle / 63, cycle % 63);
            }
        } else {
            printf("#%ld\n", time);
            if (prev.addr != d.addr) {
                printf("b%s !\n", bin(d.addr, 16, buffer));
            }
            if (prev.data != d.data) {
                printf("b%s #\n", bin(d.data, 8, buffer));
            }
            if ((d.flags & 0x80) == 0) {
                if ((cycle % 63) == 0) {
                    printf("b%s $\n", bin(cycle / 63, 9, buffer));
                }
                printf("b%s *\n", bin(cycle % 63, 6, buffer));
            }
            if (((d.flags & 0xA1) == 0xA0) && ((d.addr & 0xFC3F) >= 0xD000) && ((d.addr & 0xFC3F) <= 0xD02F)) {
                printf("b%s %c\n", bin(d.data, 8, buffer), 56+(d.addr & 0x3F));
            }
/*
            else {
                if ((cycle % 63) == 57) {
                    fprintf(stderr, "Line: %3d, Addr: %04x\n", cycle/63, d.addr);
                }
            }
*/

            uint8_t change = prev.flags ^ d.flags;
            for(b=0;b<8;b++) {
                if((change & 1)||(i==0)) {
                    printf("%c%c\n", ((d.flags >> b) & 1)+48, 48+b);
                }
                change >>= 1;
            }
            prev = d;
        }
    }
}

int main(int argc, char **argv)
{
    FILE *fi;
    
    if(argc < 2) {
        printf("Usage: dump_vcd [-t] [-w trig] <file>\n");
        exit(1);
    }
    int i = 1;
    int text_mode = 0;
    int length = 63*2*312*85;
    int trigger = -1;
    if (strcmp(argv[i], "-t") == 0) {
        i++;
        text_mode = 1;
        length = 63*2*312*3;
    }
    if (strcmp(argv[i], "-w") == 0) {
        i++;
        trigger = strtol(argv[i], NULL, 16);
        i++;
    }
    fi = fopen(argv[i], "rb");
    if(fi) {
//    	fseek(fi, 0xe6c000, SEEK_SET);
    	fseek(fi, 0x000000, SEEK_SET);
    	dump_trace(fi, length, text_mode, trigger);
    } else {
        fprintf(stderr, "Can't open file.\n");
        exit(2);
    }
    fclose(fi);
    return 0;
}
