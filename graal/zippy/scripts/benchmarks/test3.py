# The Computer Language Benchmarks Game
# http://shootout.alioth.debian.org/
#
# contributed by Tupteq

import sys

def main():
    cout = sys.stdout.write
    #size = 500
    size = int(sys.argv[1])
    xr_size = xrange(size)
    xr_iter = xrange(50)
    bit = 128
    byte_acc = 0

    cout("P4\n%d %d\n" % (size, size))
    print ("xr_size", xr_size)

    size = float(size)
    for y in xr_size:
        fy = 2j * y / size - 1j
        for x in xr_size:
            print "Middle for"
            z = 0j
            c = 2. * x / size - 1.5 + fy

            for i in xr_iter:
                print("z " , z , " c ", c , "result " , z * z + c , "abs(z)", abs(z))
                z = z * z + c
                if abs(z) >= 2.0:
                    break
            else:
                #print("else")
                byte_acc += bit
                #print("else")
                #print(bit)
                #print(byte_acc) # byte_acc are not the same at some point
                # byte_acc are the same (checked)

            if bit > 1:
                bit >>= 1
            else:
                cout(chr(byte_acc))
                bit = 128
                byte_acc = 0

        if bit != 128:
            #print(byte_acc)
            cout(chr(byte_acc))
            bit = 128
            byte_acc = 0

main()

print()
