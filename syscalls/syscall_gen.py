import sys
from keystone import *
import re

LINE_R = re.compile(r'^(\w+)=\(([0-9A-F]{2}):([0-9A-F]{2})\)$')


def main(path):
    items = '<patternlist>\n'

    with open(path) as r:
        for line in r.readlines():
            m = LINE_R.match(line)

            if m is not None:
                n = m.group(1)
                t = int(m.group(2), 16)
                v = int(m.group(3), 16)

                comp = 'li $t2,0x%02X\njr $t2\nli $t1,0x%02X' % (t, v)

                try:
                    ks = Ks(KS_ARCH_MIPS, KS_MODE_MIPS32 + KS_MODE_LITTLE_ENDIAN)
                    enc, _ = ks.asm(comp.encode())
                    enc = enc[:8] + enc[-4:]

                    items += '  <pattern>\n' \
                             '    <data>%s</data>\n    <funcstart label="%s" validcode="function"/>\n' \
                             '  </pattern>\n\n' % (' '.join('0x%02x' % x for x in enc), n)
                except KsError as e:
                    pass
                    # print('Err: %s' % e)

    items += '\n\n</patternlist>'

    print(items)


if __name__ == '__main__':
    # for i in range(0x1E, 0x80):
    #     print('jump_to_00000000h=(C0:%02X)' % i)

    main(sys.argv[1])
