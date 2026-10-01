#pragma once
namespace ApShop {
inline int ShopTier(int row,int level) {
    if((row==1 && level==1) || (row==1 && level==2) || (row==1 && level==3) || (row==3 && level==1) || (row==6 && level==1) || (row==9 && level==1) || (row==11 && level==1) || (row==2 && level==2) || (row==17 && level==1) || (row==20 && level==1) || (row==21 && level==1) || (row==26 && level==1) || (row==28 && level==1) || (row==29 && level==1) || (row==30 && level==1) || (row==39 && level==1) || (row==40 && level==1) || (row==53 && level==1) || (row==54 && level==1) || (row==55 && level==1) || (row==0 && level==1) || (row==14 && level==1)) return 1;
    if((row==1 && level==4) || (row==1 && level==5) || (row==1 && level==6) || (row==3 && level==2) || (row==4 && level==1) || (row==6 && level==2) || (row==7 && level==1) || (row==9 && level==2) || (row==10 && level==1) || (row==11 && level==2) || (row==12 && level==1) || (row==15 && level==1) || (row==18 && level==1) || (row==31 && level==1) || (row==26 && level==2) || (row==41 && level==1) || (row==42 && level==1) || (row==43 && level==1) || (row==56 && level==1) || (row==57 && level==1) || (row==58 && level==1) || (row==0 && level==2)) return 2;
    return 3;
}
inline int UnlockedShopTier(unsigned sectors) { int count=0; for(;sectors;sectors>>=1) count+=sectors&1; return count>=3?3:count>=2?2:1; }
}
