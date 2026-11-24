/* SPDX-License-Identifier: MIT */
#include "s2_input_301b.h"
/* Physical button positions independently matched to stock XInput builder
 * 0xebfc and VtnDenis' same-SHA v1.09 packer. Nintendo layout by position. */
void s2_convert_301b(s2_input *out,const s2_calibrated_301b *in) {
    static const uint8_t map[18]={
        6,15,7,14,2,3,9,10,8,11,12,4,1,0,13,5,255,16
    };
    unsigned i;
    out->buttons[0]=out->buttons[1]=out->buttons[2]=0;
    for(i=0;i<18;++i) if(map[i]!=255 && (in->buttons&(1u<<i)))
        out->buttons[map[i]>>3]|=(uint8_t)(1u<<(map[i]&7));
    /* Trigger bits already follow the stock digital threshold. Do not invent
     * a new analog dead zone or change its calibration. */
    out->lx=(uint16_t)((65535u-in->axes[0])>>4);
    out->ly=(uint16_t)((65535u-in->axes[1])>>4);
    out->rx=(uint16_t)((65535u-in->axes[2])>>4);
    out->ry=(uint16_t)((65535u-in->axes[3])>>4);
}
