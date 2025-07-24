#include "matrix.h"

void vectorFromAngle(float angle, float length, Vector2D* output) {
  output->x = std::cos(angle) * length;
  output->y = std::sin(angle) * length;
}

void vectorFromAngle(float angle, Vector2D* output) {
  output->x = std::cos(angle);
  output->y = std::sin(angle);
}

float dotProduct(const Vector2D* a, const Vector2D* b) {
  return a->x * b->x + a->y * b->y;
}

float dotProduct(const Vector2D* a, float angle) {
  Vector2D b;
  vectorFromAngle(angle, &b);
  return dotProduct(a, &b);
}

void normalizeVector(Vector2D* input, Vector2D* output) {
  float invLen = 1.0f/std::sqrt(input->x*input->x + input->y*input->y);
  output->x *= invLen;
  output->y *= invLen;
}

void multMV(Matrix2D* matrix, Vector2D* vector, Vector2D* output) {
  output->x = matrix->a * vector->x + matrix->b * vector->y;
  output->y = matrix->c * vector->x + matrix->d * vector->y;
}

void multMF(Matrix2D* matrix, float value, Matrix2D* output) {
  output->a = matrix->a * value;
  output->b = matrix->b * value;
  output->c = matrix->c * value;
  output->d = matrix->d * value;
}

void addVV(Vector2D* vector1, Vector2D* vector2, Vector2D* output) {
  output->x = vector1->x + vector2->x;
  output->y = vector1->y + vector2->y;
}

void subVV(Vector2D* vector1, Vector2D* vector2, Vector2D* output) {
  output->x = vector1->x - vector2->x;
  output->y = vector1->y - vector2->y;
}

void multMV(Matrix2Di* matrix, Vector2Di* vector, Vector2Di* output) {
  output->x = matrix->a * vector->x + matrix->b * vector->y;
  output->y = matrix->c * vector->x + matrix->d * vector->y;
  output->x = output->x >> 8;
  output->y = output->y >> 8;
}

void multMF(Matrix2Di* matrix, int32_t value, Matrix2Di* output) {
  output->a = (matrix->a * value) >> 8;
  output->b = (matrix->b * value) >> 8;
  output->c = (matrix->c * value) >> 8;
  output->d = (matrix->d * value) >> 8;
}

void addVV(Vector2Di* vector1, Vector2Di* vector2, Vector2Di* output) {
  output->x = vector1->x + vector2->x;
  output->y = vector1->y + vector2->y;
}

void subVV(Vector2Di* vector1, Vector2Di* vector2, Vector2Di* output) {
  output->x = vector1->x - vector2->x;
  output->y = vector1->y - vector2->y;
}

void invertM(Matrix2D* in, Matrix2D* out) {
  float det = in->a * in->d - in->b * in->c;
  if (fabs(det) < 1e-6) return; // Non-invertible

  out->a =  in->d / det;
  out->b = -in->b / det;
  out->c = -in->c / det;
  out->d =  in->a / det;
}