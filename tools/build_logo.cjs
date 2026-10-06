// Convert the original logo into an LVGL monochrome UI asset, preserving its geometry.
const fs=require('fs');
const sharp=require(process.env.SHARP_MODULE || 'sharp');
(async()=>{
 const {data,info}=await sharp('assets/polar-bear-strength-original.png').ensureAlpha().raw().toBuffer({resolveWithObject:true});
 const mask=Buffer.alloc(info.width*info.height);let left=info.width,top=info.height,right=0,bottom=0;
 for(let y=0;y<info.height;y++)for(let x=0;x<info.width;x++){const i=y*info.width+x,j=i*4;mask[i]=Math.round(data[j+3]*(1-(data[j]+data[j+1]+data[j+2])/765));if(mask[i]>8){left=Math.min(left,x);right=Math.max(right,x);top=Math.min(top,y);bottom=Math.max(bottom,y);}}
 const result=await sharp(mask,{raw:{width:info.width,height:info.height,channels:1}}).extract({left,top,width:right-left+1,height:bottom-top+1}).resize({width:300}).raw().toBuffer({resolveWithObject:true});
 // sharp returns RGB for this grayscale input on some builds; use the first channel.
 const {width,height,channels}=result.info;const bytes=[];for(let i=0;i<width*height;i++)bytes.push(result.data[i*channels]);
 let out='#include <lvgl.h>\nstatic const uint8_t logo_alpha[] = {\n';for(let i=0;i<bytes.length;i+=24)out+=bytes.slice(i,i+24).join(',')+',\n';out+='};\nconst lv_image_dsc_t polar_bear_logo = {\n.header = {.magic=LV_IMAGE_HEADER_MAGIC, .cf=LV_COLOR_FORMAT_A8, .flags=0, .w='+width+', .h='+height+', .stride='+width+'},\n.data_size=sizeof(logo_alpha), .data=logo_alpha\n};\n';
 fs.writeFileSync('firmware/VoltraKnob/src/ui/polar_bear_logo.c',out);console.log(`LVGL logo: ${width} x ${height}`);
})();
