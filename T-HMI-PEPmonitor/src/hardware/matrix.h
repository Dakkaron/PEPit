#ifndef __MATRIX_H__
#define __MATRIX_H__
#include "Arduino.h"

typedef struct {
  float a;
  float b;
  float c;
  float d;
} Matrix2D;

typedef struct {
  int32_t a;
  int32_t b;
  int32_t c;
  int32_t d;
} Matrix2Di;

typedef struct {
  float x;
  float y;
} Vector2D;

typedef struct {
  float x;
  float y;
  float z;
} Vector3D;

typedef struct {
  int32_t x;
  int32_t y;
} Vector2Di;

void vectorFromAngle(float angle, Vector2D* output);
void vectorFromAngle(float angle, float length, Vector2D* output);
float dotProduct(const Vector2D* a, const Vector2D* b);
float dotProduct(const Vector2D* a, float angle);

void normalizeVector(Vector2D* input, Vector2D* output);

void multMV(Matrix2D* matrix, Vector2D* vector, Vector2D* output);
void multMF(Matrix2D* matrix, float value, Matrix2D* output);
void addVV(Vector2D* vector1, Vector2D* vector2, Vector2D* output);
void subVV(Vector2D* vector1, Vector2D* vector2, Vector2D* output);

void multMV(Matrix2Di* matrix, Vector2Di* vector, Vector2Di* output);
void multMF(Matrix2Di* matrix, int32_t value, Matrix2Di* output);
void addVV(Vector2Di* vector1, Vector2Di* vector2, Vector2Di* output);
void subVV(Vector2Di* vector1, Vector2Di* vector2, Vector2Di* output);

#endif /*__MATRIX_H__*/