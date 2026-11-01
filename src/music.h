/* Small music helpers shared by the SYNTH app and PULSE CROSS */
#pragma once

#include <Arduino.h>

struct Scale {
  const char *name;
  int8_t steps[5];     // semitones above the root
};

static const Scale scales[] = {
  {"MAJOR", {0, 2, 4, 7, 9}},       // major pentatonic
  {"MINOR", {0, 3, 5, 7, 10}},      // minor pentatonic
  {"BLUES", {0, 3, 5, 6, 10}},
  {"JAPAN", {0, 1, 5, 7, 8}},       // in-sen flavour
  {"ARABIC", {0, 1, 4, 5, 7}},
};
static const int numScales = sizeof(scales) / sizeof(scales[0]);

inline float midiToFreq(float midi){
  return 440.0f * powf(2.0f, (midi - 69.0f) / 12.0f);
}

// e.g. 61 -> "C#4"
inline void noteName(int midi, char *buf, size_t len){
  static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  snprintf(buf, len, "%s%d", names[((midi % 12) + 12) % 12], midi / 12 - 1);
}

// midi note of scale degree d (may be negative or above 4, wraps octaves)
inline int scaleNote(const Scale &s, int root, int degree){
  int octave = (degree >= 0) ? degree / 5 : -((4 - degree) / 5);
  int idx = degree - octave * 5;
  return root + octave * 12 + s.steps[idx];
}
