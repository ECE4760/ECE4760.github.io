// Draw an oval (ellipse)
void drawOval(short x0, short y0, short rx, short ry, char color) {
  /* Draw an ellipse outline with center (x0,y0) and radii rx, ry
   * Parameters:
   * x0: x-coordinate of center of ellipse.
   * y0: y-coordinate of center of ellipse.
   * rx: radius in x-direction
   * ry: radius in y-direction
   * color: 4-bit color value for the ellipse outline
   */
  if (rx <= 0 || ry <= 0)
    return; // Invalid radii int x, y; int rx2=rx * rx; int ry2=ry * ry; int
            // twoRx2=2 * rx2; int
  twoRy2 = 2 * ry2;
  int p;
  int px = 0;
  int py =
      twoRx2 * ry; // Region 1 x=0; y=ry; p=(int)(ry2 - rx2 * ry + 0.25 * rx2 +
        0.5); // Initial decision parameter while (px < py) { drawPixel(x0 + x, y0 + y, color); drawPixel(x0 - x, y0 + y,
        color);
        drawPixel(x0 + x, y0 - y, color);
        drawPixel(x0 - x, y0 - y, color);
        x++;
        px += twoRy2;
        if (p < 0) {
          p += ry2 + px;
        } else {
          y--;
          py -= twoRx2;
          p += ry2 + px - py;
        }
} // Region 2 p=(int)(ry2 * (x + 0.5) * (x + 0.5) + rx2 * (y -
        1) * (y - 1) - rx2 * ry2 + 0.5);
        while (y >= 0) {
          drawPixel(x0 + x, y0 + y, color);
          drawPixel(x0 - x, y0 + y, color);
          drawPixel(x0 + x, y0 - y, color);
          drawPixel(x0 - x, y0 - y, color);

          y--;
          py -= twoRx2;
          if (p > 0) {
            p += rx2 - py;
          } else {
            x++;
            px += twoRy2;
            p += rx2 - py + px;
          }
        }
