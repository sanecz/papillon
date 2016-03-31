#include <alsa/asoundlib.h>
#include <fftw3.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>
#include <assert.h>

#define SAMPLE_SIZE 2048
#define SOUND_RATE 44100

struct fftw {
  size_t outlen;
  double *in;
  fftw_complex *bin;
  fftw_plan plan;
};

union color {
  unsigned int c;
  struct argb {
    unsigned char b;
    unsigned char g;
    unsigned char r;
    unsigned char a;
  } argb;
};

short int get_frame(char *buffer, int i) {
  return (buffer[2 * i] & 0xFF) + ((buffer[2 * i + 1] & 0xFF) << 8);
}

/* 
 * This part is made for working with the headset Steelseries Sibera Raw
 * I've never tested this part of code on other product of Steelseries.
 * The last color set before disconnecting the headset will not be preserved
 * and it will return to the last color that have been configured via
 * the Steelseries engine.
 *
 * We directly write into the HID device (generally somewhere in /dev/hidrawX)
 */
int send_color(union color color, int fd) {
  unsigned char const setup[16] = {0x01, 0x00, 0x95, 0x02, 0x80, 0xbf};
  unsigned char const content[16] = {0x01, 0x00, 0x80, 0x02, 0x52, 0x20};
  unsigned char const teardown[16] = {0x01, 0x00, 0x93, 0x02, 0x03, 0x80};
  unsigned char color_content[16] = {0x01, 0x00, 0x83, 0x03}; 
  int ret;

  color_content[4] = color.argb.r;
  color_content[5] = color.argb.g;
  color_content[6] = color.argb.b;

  if ((ret = write(fd, setup, 16)) < 0)
    return ret;
  if ((ret = write(fd, content, 16)) < 0)
    return ret;
  if ((ret = write(fd, color_content, 16)) < 0)
    return ret;
  if ((ret = write(fd, teardown, 16)) < 0)
    return ret;
  return 0;
}

int clamp(int f, int min, int max) {
  if (f < min)
    return 0;
  else if (f > max)
    return 255;
  return f;
}

// h [0, 360] s [0, 1] v [0, 1]
void hsv_to_rgb(union color *color, double h, double s, double v) {
  int i;
  float f, p, q, t;
  float r, g, b;
  
  if (s == 0) {
    r = g = b = v;
  } else {
    h /= 60;
    i = floor(h);
    f = h - i;
    p = v * (1 - s);
    q = v * (1 - s * f);
    t = v * (1 - s * (1 - f));
    switch (i) {
    case 0:
      r = v; g = t; b = p; break;
    case 1:
      r = q; g = v; b = p; break;
    case 2:
      r = p; g = v; b = t; break;
    case 3:
      r = p; g = q; b = v; break;
    case 4:
      r = t; g = p; b = v; break;
    default: // 5
      r = v; g = p; b = q; break;
    }
  }
  color->argb.r = (unsigned char)clamp(floor(r * 255), 0, 255);
  color->argb.g = (unsigned char)clamp(floor(g * 255), 0, 255);
  color->argb.b = (unsigned char)clamp(floor(b * 255), 0, 255);
}



void soundinit(snd_pcm_t **handle, char **buffer, char const *name) {
  int dir = 0;
  snd_pcm_hw_params_t *params;
  unsigned int sound_rate = SOUND_RATE;
  snd_pcm_uframes_t frames = SAMPLE_SIZE;
  
  // name can be a sound device like "hw:0,0" or "default"
  assert(snd_pcm_open(handle, name, SND_PCM_STREAM_CAPTURE, 0) >= 0);

  // alloc on the stack, we will never use anymore params, we can loose it
  snd_pcm_hw_params_alloca(&params);
  // fill with current params
  assert(snd_pcm_hw_params_any(*handle, params) >= 0);

  // interleaved, alternate L and R channels
  assert(snd_pcm_hw_params_set_access(*handle, params, SND_PCM_ACCESS_RW_INTERLEAVED) >= 0);
  // 16 bit little endian
  assert(snd_pcm_hw_params_set_format(*handle, params, SND_PCM_FORMAT_S16_LE) >= 0);
  // only one channel : mono
  assert(snd_pcm_hw_params_set_channels(*handle, params, 1) >= 0);
  // sampling rate set to 44,1kHz
  assert(snd_pcm_hw_params_set_rate_near(*handle, params, &sound_rate, &dir) >= 0);
  // size of period
  assert(snd_pcm_hw_params_set_period_size_near(*handle, params, &frames, &dir) >= 0);  
  // apply params
  assert(snd_pcm_hw_params(*handle, params) >= 0);
  // set blocking mode
  assert(snd_pcm_nonblock(*handle, 0) >= 0);

  // Sample size * 2 because 2 bytes/sample
  *buffer = (char *)malloc(SAMPLE_SIZE * 2);
  assert(*buffer != NULL);

  // prepare before read
  assert(snd_pcm_prepare(*handle) >= 0);
}

void soundfree(snd_pcm_t *handle, char *buffer) {
  snd_pcm_drop(handle);
  snd_pcm_close(handle);
  free(buffer);
}

void fftwfree(struct fftw *fftw) {
  free(fftw->in);
  free(fftw->bin);
  fftw_destroy_plan(fftw->plan);
  fftw_cleanup();
}

void fftwinit(struct fftw *fftw) {
  fftw->outlen = SAMPLE_SIZE / 2;
  fftw->in = fftw_malloc(sizeof(double) * SAMPLE_SIZE);
  assert(fftw->in != NULL);
  fftw->bin = fftw_malloc(sizeof(fftw_complex) * (fftw->outlen + 1));
  assert(fftw->bin != NULL);
  fftw->plan = fftw_plan_dft_r2c_1d(SAMPLE_SIZE, fftw->in, fftw->bin, FFTW_ESTIMATE);  
}

int main(int argc, char **argv) {
  snd_pcm_sframes_t frames;
  snd_pcm_t *handle;
  int fd;
  size_t i;
  char *buffer;
  struct fftw fftw;
  union color color, old_color;
  double magnitude_max, db_max, freq, hue, brightness;
  char *device = "/dev/hidraw0";
  char *name = "default";
  if (argc > 1)
    device = argv[1];
  
  fd = open(device,  O_RDWR|O_NONBLOCK);
  assert(fd > 0);

  if (argc > 2)
    name = argv[2];
  soundinit(&handle, &buffer, name);
  fftwinit(&fftw);

  // TODO: Takes too much cpu, use snd_pcm_readn
  // read until the buffer is full before doing something (blocking mode)
  while ((frames = snd_pcm_readi(handle, buffer, SAMPLE_SIZE)) > 0) {
    snd_pcm_drop(handle);
    snd_pcm_prepare(handle);

    for (i = 0; i < SAMPLE_SIZE; i++) {
	short int val = get_frame(buffer, i);
	fftw.in[i] = 2 * (double)val/(256 * 256);
    }

    fftw_execute(fftw.plan);

    magnitude_max = 0.0;
    db_max = 0.0;
    color.c = 0;
    
    for (i = 0; i < fftw.outlen; i++) {
      // sqrt(real * real + imaginary * imaginary)
      double magnitude = sqrt(fftw.bin[i][0] * fftw.bin[i][0] + fftw.bin[i][1] * fftw.bin[i][1]);
      double db =  20 * log10(magnitude);
      if (magnitude > magnitude_max)
	magnitude_max = magnitude;
	if (db > db_max)
	  db_max = db;
    }

    freq = magnitude_max * SOUND_RATE / fftw.outlen;
    hue = (freq * 360 / (SOUND_RATE / 2)) * 1.2;
    brightness = log(db_max) / log(90);
    hsv_to_rgb(&color, hue, 1.0, brightness);

    if (old_color.c != color.c)
      send_color(color, fd);
    old_color.c = color.c;

  }

  close(fd);
  fftwfree(&fftw);
  soundfree(handle, buffer);
  return 0;
}
