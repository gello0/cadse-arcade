/* Random maze generator shared by the maze games
 *
 * Cells store their walls as bits. The maze is a perfect maze (exactly one
 * path between any two cells) unless braid() knocks out dead ends to create
 * loops.
 */
#pragma once

#include <Arduino.h>

const uint8_t WALL_N = 1;
const uint8_t WALL_E = 2;
const uint8_t WALL_S = 4;
const uint8_t WALL_W = 8;

const int mazeMaxCols = 16;
const int mazeMaxRows = 8;

struct Maze {
  int cols, rows;
  uint8_t walls[mazeMaxRows][mazeMaxCols];

  void generate(int c, int r);           // recursive backtracker from cell 0,0
  void braid(int percent);               // remove that share of dead ends
  bool open(int x, int y, int dir) const; // dir 0 = N, 1 = E, 2 = S, 3 = W
  int countWalls(int x, int y) const;
  void removeWall(int x, int y, int dir);
};
