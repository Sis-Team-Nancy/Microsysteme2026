$fn = 80;
// -------- CHOIX DE LA PIECE --------
part = "base";
// -------- BOITIER PRINCIPAL --------
box_L = 65;
box_W = 100;
box_H = 28;
corner_r = 9;
wall     = 2.2;
floor_t  = 2.2;
offset_x = 12.5;
// -------- USB-C XIAO (face avant Y-) --------
usb_w = 12;
usb_h = 7;
usb_x = 0;       // centré en X sur la petite face
usb_z = 14;
// -------- DIMENSIONS PCB --------
pcb_L = 34;
pcb_W = 54;
pcb_corner = 2.5;
hole_dx = pcb_L - 2 * pcb_corner;
hole_dy = pcb_W - 2 * pcb_corner;
// -------- SUPPORTS PCB --------
pcb_x = 0;
pcb_y = 0;
peg_d = 1.8;
peg_h = 4.0;
// -------- OUTILS --------
module rr2d(L, W, r){
  r2 = min(r, min(L,W)/2 - 0.01);
  hull(){
    translate([ L/2 - r2,  W/2 - r2]) circle(r=r2);
    translate([-L/2 + r2,  W/2 - r2]) circle(r=r2);
    translate([ L/2 - r2, -W/2 + r2]) circle(r=r2);
    translate([-L/2 + r2, -W/2 + r2]) circle(r=r2);
  }
}
module rr3d(L, W, H, r){
  linear_extrude(height=H) rr2d(L,W,r);
}
// -------- SUPPORTS PCB (4 coins) --------
module pcb_mount(){
  positions = [
    [ hole_dx/2,  hole_dy/2],
    [-hole_dx/2,  hole_dy/2],
    [ hole_dx/2, -hole_dy/2],
    [-hole_dx/2, -hole_dy/2]
  ];
  for (p = positions){
    translate([pcb_x + p[0], pcb_y + p[1], floor_t])
      cylinder(d=peg_d, h=peg_h);
  }
}
// -------- BASE COMPLÈTE --------
module base_open(){
  difference(){
    rr3d(box_L, box_W, box_H, corner_r);
    // Cavité intérieure
    translate([0,0,floor_t])
      rr3d(box_L-2*wall, box_W-2*wall,
           box_H-floor_t+0.3,
           max(corner_r-wall,1));
    // TROU USB-C (face avant Y-, centré en X)
    translate([usb_x, -box_W/2 - 0.01, usb_z])
      cube([usb_w, wall+6, usb_h], center=true);
  }
  // 4 plots de support PCB
  pcb_mount();
}
// -------- BASE DÉCALÉE --------
module base_open_shifted(){
  translate([offset_x,0,0])
    base_open();
}
// -------- RENDU FINAL --------
base_open_shifted();