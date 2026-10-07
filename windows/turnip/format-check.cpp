#include <cstdio>
#include "freedreno/vulkan/tu_formats.h"

int main()
{
   unsigned failures = 0;
   for (unsigned value = 0; value < 256; value++) {
      tu_native_format format = {static_cast<a6xx_format>(value), WZYX};
      if (static_cast<unsigned>(format.fmt) != value) {
         if (failures < 4)
            std::printf("Format 0x%02x became 0x%08x\n", value,
                        static_cast<unsigned>(format.fmt));
         failures++;
      }
   }
   std::printf("%s: 256 hardware format values, %u mismatches\n",
               failures ? "FAIL" : "PASS", failures);
   return failures ? 1 : 0;
}
