#ifndef _DDP_MATRIX_PARA_H_
#define _DDP_MATRIX_PARA_H_


#define TABLE_NO 10

#define YUV2RGB_601_16_16  0
#define YUV2RGB_601_16_0   1
#define YUV2RGB_601_0_0    2
#define YUV2RGB_709_16_16  3
#define YUV2RGB_709_16_0   4
#define YUV2RGB_709_0_0    5
#define RGB2YUV_601        6
#define RGB2YUV_601_XVYCC  7
#define RGB2YUV_709        8
#define RGB2YUV_709_XVYCC  9

static const short int coef_rdma_601_r2y[5][3] = 
{
  {263, 516, 100},
  {-152, -298, 450},
  {450, -377, -73},
  {0, 0, 0},
  {0, 128, 128}  
};

static const short int coef_rdma_709_r2y[5][3] = 
{
  {187, 629, 63},
  {-103, -347, 450},
  {450, -409, -41},
  {0, 0, 0},
  {16, 128, 128} 
};

static const short int coef_rdma_601_y2r[5][3] = 
{
  {1193, 0, 1633},
  {1193, -400, -832},
  {1193, 2065, 0},
  {-16, -128, -128},
  {0, 0, 0}  
};

static const short int coef_rdma_709_y2r[5][3] = 
{
  {1193, 0, 1934},
  {1193, -217, -545},
  {1193, 2163, -1},
  {-16, -128, -128},
  {0, 0, 0}  
};


static const short int coef[10][5][3] = 
  //                                         
  //              
  //                                         
  //                                                        
  { { { 0x0400, 0x0000, 0x057c },   //                                          
      { 0x0400, 0x1ea8, 0x1d35 },   //                                          
      { 0x0400, 0x06ee, 0x0000 },   //                                          
      {      0, 0x180 , 0x180  },   //                                         
      {      0,      0,      0 } },
  //                                                 
    { { 0x04a7, 0x0000, 0x0662 },   //                                          
      { 0x04a7, 0x1e70, 0x1cc0 },   //                                          
      { 0x04a7, 0x0812, 0x0000 },   //                                          
      { 0x1f0 , 0x180 , 0x180  },   //                                         
      {      0,      0,      0 } },
  //                                       
    { { 0x0400, 0x0000, 0x059b },   //                                          
      { 0x0400, 0x1ea0, 0x1d25 },   //                                          
      { 0x0400, 0x0716, 0x0000 },   //                                          
      {      0, 0x180 , 0x180  },   //                                         
      {      0,      0,      0 } },
  //                                         
  //              
  //                                         
  //                                                        
    { { 0x0400, 0x0000, 0x0628 },  //                                           
      { 0x0400, 0x1f45, 0x1e2a },  //                                           
      { 0x0400, 0x0743, 0x0000 },  //                                           
      {      0, 0x180 , 0x180  },  //                                          
      {      0,      0,      0 } },
  //                                                 
    { { 0x04a7, 0x0000, 0x072c },  //                                           
      { 0x04a7, 0x1f26, 0x1dde },  //                                           
      { 0x04a7, 0x0875, 0x0000 },  //                                           
      { 0x1f0 , 0x180 , 0x180  },   //                                         
      {      0,      0,      0 } },
  //                                       
    { { 0x0400, 0x0000, 0x064d },  //                                           
      { 0x0400, 0x1f40, 0x1e21 },  //                                           
      { 0x0400, 0x076c, 0x0000 },  //                                           
      {      0, 0x180 , 0x180  },  //                                          
      {      0,      0,      0 } },
  //                                         
  //              
  //                                         
  //                                                  
    { { 0x0107, 0x0204, 0x0064 },   //                                          
      { 0x1f68, 0x1ed6, 0x01c2 },   //                                          
      { 0x01c2, 0x1e87, 0x1fb7 },   //                                          
      {      0,      0,      0 },  
      { 0x010 , 0x080 , 0x080  } }, //                                         
  //                                       
    { { 0x0132, 0x0259, 0x0075 },  //                                           
      { 0x1f53, 0x1ead, 0x0200 },  //                                           
      { 0x0200, 0x1e53, 0x1fad },  //                                           
      {      0,      0,      0 },  
      { 0x010 , 0x080 , 0x080  } }, //                                         
  //                                         
  //              
  //                                         
  //                                                  
    { { 0x00bb, 0x0275, 0x003f },   //                                          
      { 0x1f98, 0x1ea6, 0x01c2 },   //                                          
      { 0x01c2, 0x1e67, 0x1fd7 },   //                                          
      {      0,      0,      0 },  
      { 0x010 , 0x080 , 0x080  } }, //                                         
  //                                       
    { { 0x00da, 0x02dc, 0x004a },  //                                           
      { 0x1f8b, 0x1e75, 0x0200 },  //                                           
      { 0x0200, 0x1e2f, 0x1fd1 },  //                                           
      {      0,      0,      0 },  
      { 0x010 , 0x080 , 0x080  } }  //                                         
  };

#endif
