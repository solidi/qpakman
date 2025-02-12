//------------------------------------------------------------------------
//  Miptex creation
//------------------------------------------------------------------------
//
//  Copyright (c) 2008  Andrew J Apted
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//------------------------------------------------------------------------

#include "headers.h"
#include "main.h"

#include <map>

#include "im_color.h"
#include "im_image.h"
#include "im_mip.h"
//#include "im_png.h"
#include "pakfile.h"
#include "q1_structs.h"


extern std::vector<std::string> input_names;

extern bool opt_force;


std::map<std::string, int> all_lump_names;



rgb_image_c *MIP_LoadImage(const char *filename)
{
  // Note: extension checks are case-insensitive

  if (CheckExtension(filename, "bmp") || CheckExtension(filename, "tga") ||
      CheckExtension(filename, "gif") || CheckExtension(filename, "pcx") ||
      CheckExtension(filename, "pgm") || CheckExtension(filename, "ppm") ||
      CheckExtension(filename, "tif") || CheckExtension(filename, "tiff"))
  {
    printf("FAILURE: Unsupported image format\n");
    return NULL;
  }

  if (CheckExtension(filename, "jpg") ||
      CheckExtension(filename, "jpeg"))
  {
    // TODO: JPEG
    printf("FAILURE: JPEG image format not supported yet\n");
    return NULL;
  }

  if (! CheckExtension(filename, "png"))
  {
    printf("FAILURE: Not an image file\n");
    return NULL;
  }


  FILE *fp = fopen(filename, "rb");

  if (! fp)
  {
    printf("FAILURE: Cannot open image file: %s\n", filename);
    return NULL;
  }

  //rgb_image_c * img = PNG_Load(fp);
  rgb_image_c * img = NULL;

  fclose(fp);

  return img;
}


static bool ReplacePrefix(char *name, const char *prefix, char ch)
{
  if (strncmp(name, prefix, strlen(prefix)) != 0)
    return false;

  *name++ = ch;

  int move_chars = strlen(prefix) - 1;

  if (move_chars > 1)
  {
    int new_len = strlen(name) - move_chars;

    // +1 to copy the trailing NUL as well
    memmove(name, name + move_chars, new_len + 1);
  }

  return true;
}

static void ApplyAbbreviations(char *name, bool *fullbright)
{
  // make it lower case
  for (char *u = name; *u; u++)
    *u = tolower(*u);

  int len = strlen(name);

  if (len >= 6)
  {
       ReplacePrefix(name, "star_", '*')
    || ReplacePrefix(name, "plus_", '+')
    || ReplacePrefix(name, "minu_", '-')
    || ReplacePrefix(name, "divd_", '/');
  }

  len = strlen(name);

  if (len >= 5 && memcmp(name+len-4, "_fbr", 4) == 0)
  {
    *fullbright = true;

    name[len-4] = 0;
  }
}


std::string MIP_FileToLumpName(const char *filename, bool * fullbright)
{
  char *base = ReplaceExtension(FindBaseName(filename), NULL);

  if (strlen(base) == 0)
    FatalError("Weird image filename: %s\n", filename);

  ApplyAbbreviations(base, fullbright);

  if (strlen(base) > 15)
  {
    printf("WARNING: Lump name too long, will abbreviate it\n");

    // create new name using first and last 7 characters
    char new_name[20];

    memset(new_name, 0, sizeof(new_name));
    memcpy(new_name, base, 7);
    memcpy(new_name+7, base + strlen(base) - 8, 8);

    StringFree(base);

    base = StringDup(new_name);
  }

  if (! opt_picture)
    printf("   miptex name: %s\n", base);

  // check if already exists
  if (all_lump_names.find(base) != all_lump_names.end())
  {
    printf("FAILURE: Lump already exists, will not duplicate\n");
    return std::string();
  }

  all_lump_names[base] = 1;

  std::string result(base);
  StringFree(base);

  return result;
}


void MIP_ConvertImage(rgb_image_c *img, bool dither = false)
{
  byte *line_buf = new byte[img->width];
  s16_t *err_buf = new s16_t[img->width * 3];

  memset(err_buf, 0, sizeof(s16_t) * img->width * 3);

  for (int y = 0; y < img->height; y++)
  {
    const u32_t *src   = & img->PixelAt(0, y);
    const u32_t *src_e = src + img->width;

    byte *dest = line_buf;

    if (dither)
    {
      s16_t *errs = err_buf;

      s16_t r = 0;
      s16_t g = 0;
      s16_t b = 0;

      for (; src < src_e; src++)
      {
        // add error from previous line
        r = (r + errs[0]) >> 1;
        g = (g + errs[1]) >> 1;
        b = (b + errs[2]) >> 1;

        r += RGB_R(*src);
        g += RGB_G(*src);
        b += RGB_B(*src);

        byte alpha = RGB_A(*src);

        // clamp result
        if (r & 0xF00) r = (r < 0) ? 0 : 255;
        if (g & 0xF00) g = (g < 0) ? 0 : 255;
        if (b & 0xF00) b = (b < 0) ? 0 : 255;

        // store pixel
        byte pix = COL_MapColor(MAKE_RGBA(r, g, b, alpha));

        *dest++ = pix;

        // determine new error
        u32_t got = COL_ReadPalette(pix);

        r -= RGB_R(got);
        g -= RGB_G(got);
        b -= RGB_B(got);

        *errs++ = r;
        *errs++ = g;
        *errs++ = b;
      }
    }
    else
    {
      for (; src < src_e; src++)
      {
        *dest++ = COL_MapColor(*src);
      }
    }

    WAD2_AppendData(line_buf, img->width);
  }

  delete[] line_buf;
  delete[] err_buf;
}


bool MIP_InsertPicture(rgb_image_c *img, const char *lump_name)
{
  char *upper_name = StringDup(lump_name);
  for (char *u = upper_name; *u; u++)
    *u = toupper(*u);

  printf("   pic name: %s\n", upper_name);

  COL_SetTransparent(255);
  COL_SetFullBright(true);

  WAD2_NewLump(upper_name, TYP_QPIC);

  // create PIC header
  pic_header_t pic;

  pic.width  = LE_U32(img->width);
  pic.height = LE_U32(img->height);

  WAD2_AppendData(&pic, sizeof(pic));

  MIP_ConvertImage(img);

  WAD2_FinishLump();

  StringFree(upper_name);
  return true;
}


bool MIP_InsertRawBlock(rgb_image_c *img, const char *lump_name, bool black_is_trans)
{
  char *upper_name = StringDup(lump_name);
  for (char *u = upper_name; *u; u++)
    *u = toupper(*u);

  printf("   raw block name: %s\n", upper_name);

  // ensure the correct transparency color is used
  if (black_is_trans)
    img->BlackToTrans();

  COL_SetTransparent(0);
  COL_SetFullBright(true);

  // TYP_NONE might be more appropriate here, however the gfx.wad
  // in both Quake1 and Hexen2 uses TYP_MIPTEX, so I do the same.
  WAD2_NewLump(upper_name, TYP_MIPTEX);

  MIP_ConvertImage(img);

  WAD2_FinishLump();

  StringFree(upper_name);
  return true;
}


bool MIP_ProcessImage(const char *filename)
{
  bool fullbright = false;

  std::string lump_name = MIP_FileToLumpName(filename, &fullbright);

  if (lump_name.empty())
    return false;

  rgb_image_c * img = MIP_LoadImage(filename);

  if (! img)
    return false;

  // handle gfx.wad stuff
  if (StringCaseCmp(lump_name.c_str(), "CONCHARS") == 0 ||
      StringCaseCmp(lump_name.c_str(), "TINYFONT") == 0)
  {
    bool result = MIP_InsertRawBlock(img, lump_name.c_str(), true);

    delete img;
    return result;
  }
  else if (opt_picture)
  {
    bool result = MIP_InsertPicture(img, lump_name.c_str());

    delete img;
    return result;
  }


  if ((img->width & 7) != 0 || (img->height & 7) != 0)
  {
    printf("WARNING: Image size not multiple of 8, will scale up\n");

    int new_w = (img->width  + 7) & ~7;
    int new_h = (img->height + 7) & ~7;

    printf("   new size: %dx%d\n", new_w, new_h);

    rgb_image_c *tmp = img->ScaledDup(new_w, new_h);

    delete img; img = tmp;
  }

  if (StringCaseCmpPartial(lump_name.c_str(), "sky") == 0)
  {
    img->QuakeSkyFix();
  }


  WAD2_NewLump(lump_name.c_str(), TYP_MIPTEX);


  // mip header
  miptex_t mm_tex;

  int offset = sizeof(mm_tex);

  memset(mm_tex.name, 0, sizeof(mm_tex.name));
  strcpy(mm_tex.name, lump_name.c_str());

  mm_tex.width  = LE_U32(img->width);
  mm_tex.height = LE_U32(img->height);

  for (int i = 0; i < MIP_LEVELS; i++)
  {
    mm_tex.offsets[i] = LE_U32(offset);

    int w = img->width  / (1 << i);
    int h = img->height / (1 << i);

    offset += w * h;
  }

  WAD2_AppendData(&mm_tex, sizeof(mm_tex));


  COL_SetTransparent(0);
  COL_SetFullBright(fullbright);


  // now the actual textures
  MIP_ConvertImage(img, opt_dither);

  for (int mip = 1; mip < MIP_LEVELS; mip++)
  {
    rgb_image_c *tmp = img->NiceMip();

    delete img; img = tmp;

    MIP_ConvertImage(img, true);
  }

  WAD2_FinishLump();

  delete img;
  return true;
}


void MIP_CreateWAD(const char *filename)
{
  if (input_names.size() == 0)
    FatalError("No input images were specified!\n");

  // now make the output WAD2 file!
  if (! WAD2_OpenWrite(filename))
    FatalError("Cannot create WAD2 file: %s\n", filename);

  printf("\n");
  printf("--------------------------------------------------\n");

  int failures = 0;

  for (unsigned int j = 0; j < input_names.size(); j++)
  {
    printf("Processing %d/%d: %s\n", 1+(int)j, (int)input_names.size(),
           input_names[j].c_str());

    if (! MIP_ProcessImage(input_names[j].c_str()))
      failures++;

    printf("\n");
  }

  printf("--------------------------------------------------\n");

  WAD2_CloseWrite();

  printf("Mipped %d images, with %d failures\n",
         (int)input_names.size() - failures, failures);

}


//------------------------------------------------------------------------


static const char * ExpandFileName(const char *lump_name, bool fullbright)
{
  int max_len = strlen(lump_name) + 32;

  char *result = StringNew(max_len);

  // convert any special first character
  if (lump_name[0] == '*')
  {
    strcpy(result, "star_");
  }
  else if (lump_name[0] == '+')
  {
    strcpy(result, "plus_");
  }
  else if (lump_name[0] == '-')
  {
    strcpy(result, "minu_");
  }
  else if (lump_name[0] == '/')
  {
    strcpy(result, "divd_");
  }
  else
  {
    result[0] = lump_name[0];
    result[1] = 0;
  }

  strcat(result, lump_name + 1);

  // sanitize filename (remove problematic characters)
  bool warned = false;

  for (char *p = result; *p; p++)
  {
    if (*p == ' ')
      *p = '_';

    if (*p != '_' && *p != '-' && ! isalnum(*p))
    {
      if (! warned)
      {
        printf("WARNING: removing weird characters from name (\\%03o)\n",
               (unsigned char)*p);
        warned = true;
      }

      *p = '_';
    }
  }

  if (fullbright)
    strcat(result, "_fbr");

  strcat(result, ".png");

  return result;
}


static bool Do_SaveImage(rgb_image_c *img, const char *lump_name, bool fullbright)
{
  const char *filename = ExpandFileName(lump_name, fullbright);

  if (FileExists(filename) && ! opt_force)
  {
    printf("FAILURE: will not overwrite file: %s\n\n", filename);

    StringFree(filename);
    return false;
  }

  FILE *fp = fopen(filename, "wb");

  if (! fp)
  {
    printf("FAILURE: cannot create output file: %s\n\n", filename);

    StringFree(filename);
    return false;
  }

  //bool result = PNG_Save(fp, img);
  bool result = false;

  if (! result)
    printf("FAILURE: error while writing PNG file\n\n");

  fclose(fp);

  StringFree(filename);

  return result;
}


bool MIP_ExtractMipTex(int entry, const char *lump_name)
{
  // mip header
  miptex_t mm_tex;

  if (! WAD2_ReadData(entry, 0, (int)sizeof(mm_tex), &mm_tex))
  {
    printf("FAILURE: could not read miptex header!\n\n");
    return false;
  }

  // (We ignore the internal name and offsets)

  mm_tex.width  = LE_U32(mm_tex.width);
  mm_tex.height = LE_U32(mm_tex.height);

  int width  = mm_tex.width;
  int height = mm_tex.height;

  if (width  < 8 || width  > 4096 ||
      height < 8 || height > 4096)
  {
    printf("FAILURE: weird size of image: %dx%d\n\n", width, height);
    return false;
  }

  byte *pixels = new byte[width * height];

  // NOTE: we assume that the pixels directly follow the miptex header

  if (! WAD2_ReadData(entry, (int)sizeof(mm_tex), width * height, pixels))
  {
    printf("FAILURE: could not read %dx%d pixels from miptex\n\n", width, height);
    delete[] pixels;
    return false;
  }

  // create the image for saving.
  // if the image contains fullbright pixels, the output filename
  // will be given the '_fbr' prefix.
  bool fullbright = false;

  rgb_image_c *img = new rgb_image_c(width, height);

  for (int y = 0; y < height; y++)
  for (int x = 0; x < width;  x++)
  {
    byte pix = pixels[y*width + x];

    if (pix >= 256-32)
      fullbright = true;

    img->PixelAt(x, y) = COL_ReadPalette(pix);
  }


  if (StringCaseCmpPartial(lump_name, "sky") == 0)
  {
    img->QuakeSkyFix();
  }

  bool result = Do_SaveImage(img, lump_name, fullbright);

  delete   img;
  delete[] pixels;

  return result;
}


bool MIP_ExtractPicture(int entry, const char *lump_name)
{
  pic_header_t pic;

  if (! WAD2_ReadData(entry, 0, (int)sizeof(pic), &pic))
  {
    printf("FAILURE: could not read picture header!\n\n");
    return false;
  }

  int width  = LE_U32(pic.width);
  int height = LE_U32(pic.height);

  if (width  < 1 || width  > 2048 ||
      height < 1 || height > 2048)
  {
    printf("FAILURE: weird size of picture: %dx%d\n\n", width, height);
    return false;
  }

  byte *pixels = new byte[width * height];

  if (! WAD2_ReadData(entry, (int)sizeof(pic), width * height, pixels))
  {
    printf("FAILURE: could not read %dx%d pixels from picture\n\n", width, height);
    delete[] pixels;
    return false;
  }

  // create the image for saving.
  COL_SetTransparent(255);

  rgb_image_c *img = new rgb_image_c(width, height);

  for (int y = 0; y < height; y++)
  for (int x = 0; x < width;  x++)
  {
    byte pix = pixels[y*width + x];

    img->PixelAt(x, y) = COL_ReadPalWithTrans(pix);
  }

  bool result = Do_SaveImage(img, lump_name, false /* fullbright */);

  delete   img;
  delete[] pixels;

  return result;
}


bool MIP_ExtractRawBlock(int entry, const char *lump_name)
{
  int total = WAD2_EntryLen(entry);

  // guess size
  int width, height;

  if (StringCaseCmp(lump_name, "CONCHARS") == 0)
  {
    width  = 128;
    height = 128;
  }
  else if (StringCaseCmp(lump_name, "TINYFONT") == 0)
  {
    width  = 128;
    height = 32;
  }
  else
  {
    for (width = 4096; width*width > total; width /= 2)
    { }

    height = width;
  }

  printf("  Guessing size to be: %dx%d\n", width, height);

  if (width  < 8 || width  > 2048 ||
      height < 8 || height > 2048)
  {
    printf("FAILURE: weird size of picture: %dx%d\n\n", width, height);
    return false;
  }

  byte *pixels = new byte[width * height];

  if (! WAD2_ReadData(entry, 0, width * height, pixels))
  {
    printf("FAILURE: could not read %dx%d pixels from picture\n\n", width, height);
    delete[] pixels;
    return false;
  }

  // create the image for saving
  COL_SetTransparent(0);

  rgb_image_c *img = new rgb_image_c(width, height);

  for (int y = 0; y < height; y++)
  for (int x = 0; x < width;  x++)
  {
    byte pix = pixels[y*width + x];

    img->PixelAt(x, y) = COL_ReadPalWithTrans(pix);
  }

  bool result = Do_SaveImage(img, lump_name, false /* fullbright */);

  delete   img;
  delete[] pixels;

  return result;
}


void MIP_ExtractWAD(const char *filename)
{
  if (! WAD2_OpenRead(filename))
    FatalError("Cannot open WAD2 file: %s\n", filename);

  printf("\n");
  printf("--------------------------------------------------\n");

  int num_lumps = WAD2_NumEntries();

  int skipped  = 0;
  int failures = 0;

  for (int i = 0; i < num_lumps; i++)
  {
    int type = WAD2_EntryType(i);
    const char *name = WAD2_EntryName(i);

    // special handling for two odd-ball lumps (raw pixels)
    if (StringCaseCmp(name, "CONCHARS") == 0 ||
        StringCaseCmp(name, "TINYFONT") == 0)
    {
      printf("Unpacking %d/%d (BLOCK) : %s\n", i+1, num_lumps, name);

      MIP_ExtractRawBlock(i, name);
    }
    else if (type == TYP_QPIC)
    {
      printf("Unpacking %d/%d (PIC) : %s\n", i+1, num_lumps, name);

      if (! MIP_ExtractPicture(i, name))
        failures++;
    }
    else if (type == TYP_MIPTEX)
    {
      printf("Unpacking %d/%d : %s\n", i+1, num_lumps, name);

      if (! MIP_ExtractMipTex(i, name))
        failures++;
    }
    else
    {
      printf("SKIPPING NON-MIPTEX entry %d/%d : %s\n", i+1, num_lumps, name);
      skipped++;
      continue;
    }
  }

  printf("--------------------------------------------------\n");
  printf("\n");

  WAD2_CloseRead();

  if (skipped > 0)
    printf("Skipped %d non-miptex lumps\n", skipped);

  printf("Extracted %d entries, with %d failures\n",
         num_lumps - failures - skipped, failures);
}


//------------------------------------------------------------------------


bool MIP_DecodeWAL(int entry, const char *filename)
{
  wal_header_t wal;

  if (! PAK_ReadData(entry, 0, (int)sizeof(wal), &wal))
  {
    printf("FAILURE: could not read WAL header!\n\n");
    return false;
  }

  // (We ignore the internal name)

  int width  = LE_U32(wal.width);
  int height = LE_U32(wal.height);
  int offset = LE_U32(wal.offsets[0]);

  if (width  < 8 || width  > 4096 ||
      height < 8 || height > 4096)
  {
    printf("FAILURE: weird size of image: %dx%d\n\n", width, height);
    return false;
  }

  int total = PAK_EntryLen(entry);

  if (offset < 80 || offset > total - width*height*85/64)
  {
    printf("FAILURE: invalid offset in WAL header (0x%08x)\n", offset);
    return false;
  }

  byte *pixels = new byte[width * height];

  if (! PAK_ReadData(entry, offset, width * height, pixels))
  {
    printf("FAILURE: could not read %dx%d pixels from WAL\n\n", width, height);
    delete[] pixels;
    return false;
  }

  // create the image for saving.
  COL_SetFullBright(true);

  rgb_image_c *img = new rgb_image_c(width, height);

  for (int y = 0; y < height; y++)
  for (int x = 0; x < width;  x++)
  {
    byte pix = pixels[y*width + x];

    img->PixelAt(x, y) = COL_ReadPalette(pix);
  }

  delete[] pixels;


  // TODO: transparent bits and/or SKY


  FILE *fp = fopen(filename, "wb");

  if (! fp)
  {
    printf("FAILURE: cannot create output file: %s\n\n", filename);

    delete img;
    return false;
  }

  //bool result = PNG_Save(fp, img);
  bool result = false;

  fclose(fp);

  delete img;

  if (! result)
    printf("FAILURE: error while writing PNG file\n\n");

  return result;
}


void MIP_EncodeWAL(const char *filename)
{
  FatalError("MIP_EncodeWAL: not yet implemented.\n");
}

//--- editor settings ---
// vi:ts=2:sw=2:expandtab
