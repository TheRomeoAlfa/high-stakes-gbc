-- high stakes
-- by krystian majewski, lazy devs

-- nice to have
-- - face fading
-- - chip target highlight?
-- - touch / mouse controls
-- - tutorial?

function _init()
 ver="v3"
 --debug={}
 
 wint,fadeperc,shake,frames,bignums,fadetable=240,1,0,0,{},split2d("0,0,0,0,0,0,0,0,0,0,0,0,0,0,0|1,1,129,129,129,129,129,129,129,129,0,0,0,0,0|2,2,2,130,130,130,130,130,128,128,128,128,128,0,0|128,128,128,128,128,128,128,128,0,0,0,0,0,0,0|129,129,129,129,129,129,129,129,129,0,0,0,0,0,0|5,5,133,133,133,133,130,130,128,128,128,128,128,0,0|6,6,134,13,13,13,141,5,5,5,133,130,128,128,0|7,6,6,6,134,134,134,134,5,5,5,133,130,128,0|8,8,136,136,136,136,132,132,132,130,128,128,128,128,0|136,136,136,136,136,132,132,132,132,130,128,128,128,128,0|130,130,130,130,130,130,130,128,128,128,128,128,128,0,0|11,139,139,139,139,3,3,3,3,129,129,129,0,0,0|12,12,12,140,140,140,140,131,131,131,1,129,129,129,0|13,13,141,141,5,5,5,133,133,130,129,129,128,128,0|14,14,14,134,134,141,141,2,2,133,130,130,128,128,0|15,143,143,134,134,134,134,5,5,5,133,133,128,128,0") 
 
 cartdata("highstakes")
 loadgame()
 --chain=2
 --unlocked={true,true,true,true}
 
 local bnumi,bnumx,bnumy,bnumw=split("0,1,2,3,4,5,6,7,8,9,h,i,g,s,t,a,k,e,r,o,u,n,d,w,l,y,p",",",false),split("15,23,29,37,45,53,61,69,77,85,15,23,29,45,53,61,69,77,15,23,31,39,47,55,65,72,80"),split("118,118,118,118,118,118,118,118,118,118,118,118,118,118,118,118,118,118,108,108,108,108,108,108,108,108,108"),split("8,6,8,8,8,8,8,8,8,8,8,4,8,8,8,8,8,6,8,8,8,8,8,10,7,8,8")

 for i=1,#bnumi do
  local bb={}
  bignums[bnumi[i]]={
   x=bnumx[i],
   y=bnumy[i],
   w=bnumw[i],
   alt=i>10,
   kern=bnumi[i]=="t" and -1 or 0
  }
 end

 cardx,cardy,chipx,chipy,circcols1,circcols2,circcols3,drects,chipspr,curpos,clout=split("41,63,85,41,63,85,41,63,85"),split("30,30,30,58,58,58,86,86,86"),split("36,58,80,99,99,99"),split("6,6,6,27,55,83"),split2d("90,9|41,2|26,10|15,3"),split2d("90,10|24,3"),{{90,15}},split("31,18,53,18,53,46,31,46"),{},{},{} 
 
 for ch in all(split("28,21,13,10|41,21,13,10|67,21,13,10|80,21,13,10|93,21,7,10|100,21,7,10|19,15,9,8|19,23,9,8|28,31,13,10|41,31,13,10","|")) do
  ch=split(ch)
  add(chipspr,{x=ch[1],y=ch[2],w=ch[3],h=ch[4]})
 end
 for ch in all(split("39,38,4,16,2,10|61,38,5,1,3,11|83,38,6,2,13,12|39,66,7,16,5,1|61,66,8,4,6,2|83,66,9,5,14,3|39,94,17,16,8,4|61,94,17,7,9,5|83,94,17,8,15,6|39,12,1,10,11,10|61,12,2,10,12,11|83,12,3,11,12,12|102,33,14,3,13,13|102,61,15,6,14,13|102,89,17,9,15,14|15,47,17,16,4,16|15,103,17,17,7,16","|")) do
  ch=split(ch)
  add(curpos,{x=ch[1],y=ch[2],dw=ch[3],lf=ch[4],rt=ch[5],up=ch[6]}) 
 end
 for s in all(split("5,4|5,13#5,2|5,9|5,16#1,3|9,3|1,15|9,15#1,3|9,3|1,15|9,15|5,9#1,3|9,3|1,9|9,9|1,15|9,15#1,3|9,3|1,9|9,9|1,15|9,15|5,6#1,3|9,3|1,9|9,9|1,15|9,15|5,6|5,12#1,1|9,1|1,6|9,6|1,11|9,11|1,16|9,16|5,4","#")) do
  add(clout,split2d(s))
 end
 
 
 --menu / vamp data
 mnutxt1,mnutxt2,vampmults,vampcost,vamptoken,helptxt,helpy,helpdy,talktxt,talktxtd=split("bafur,houkin,zver,orlok"),split("'i don't bite','rent is due','zamorit chervichka','blood is life!'"),split("1,5,10,100"),split("0,50,200,1000"),split("2,1,0,0"),split("place on a face-down card.\ncard value is equal or \nhigher than token number|place on a face-up card.\ncompares the values of\nadjecent face-down cards|stab a card if you think\nit is a vampire. bonus ML\nif you get it right.|flip all cards in this\ncolumn face-up to unlock\nthis hint token.|flip all cards in this line\nface-up to unlock this\nhint token.|highlights a 2x2 area where\nthe vampire card will be.","|"),0,0,"","" 
 
 cur,curx,cury,curshake,circcx,circcy,circcdx,circcdy,mnux,mnuy,mnudx,mnudy,mnucur=1,39,38,0,64.5,49.5,64.5,49.5,0,31,0,31,1

 makecircs() 

 --gen cards
 cls()
 rectfill(0,0,19,25,11)
 rrectfill(0,0,19,25,10)
 for val=2,14 do
	 rectfill(1,1,17,23,0)
	 mypal()
	 local mcl=clout[val-1]
	 for cspr in all(mcl) do
	  sspr(0,8,7,7,cspr[1]+1,cspr[2]+1)
	 end
	 palt()
	 if val>=10 then
	  sspr(28+(val-10)*15,0,15,21,2,2)
	 end
	 if val<10 then
	  rectfill(1,1,5,7,0)
	  print(val,2,2,val==10 and 8 or 6)
	 end 
	 local adr=0x4300+(val-2)*250
	 for i=0,24 do
	  memcpy(adr+i*10,0x6000+i*64,10)
	 end
 end
 
 --menucards
 mcards={}
 for i=1,4 do
  add(mcards,{
   x=64+(i-1)*39,
   y=100,
   z=1,
   zbase=1,
   val=9+i,
   hidden=false,
   rot=0,
   chips={}
  })
 end
 
 _upd,_drw=update_credits,draw_credits
 music(0)
end

function _update60()
 frames+=1
 if frames<0 then frames=0 end
 _upd()
end

function _draw()
 doshake()
 _drw()

 --cursor(1,1)
 --color(12)
 --for d in all(debug) do
 -- print(d)
 --end
 
 fadeperc=max(fadeperc-0.04,0)
 dofade()
end

function genbacks()
 for x=0,6 do
  for y=0,9 do
   local c=rnd(1)<0.4 and 9 or 2
   sset(111+x,28+y,c)
   sset(123-x,28+y,c)
   sset(111+x,46-y,c)
   sset(123-x,46-y,c)
  end
 end
end

function makecircs()
 circs={}
 for i=9,0,-1 do
  add(circs,{x=64,y=49,r=i*9,c=2,t=1,carr=circcols1})
 end
end
-->8
--draw

function draw_credits()
 cls(9)
 for _i,_t in pairs(split("made by,krystian majewski,@lazydevsacademy,,music by,grubermusic,@grubermusic,,based on a design by,tyler anderson,@tandyq")) do
  cprint(_t,63,20+_i*6,10)
 end
end

function draw_start()
 cls()
 drawcircs()
 mypal()
 circcdx,circcdy=64.5+cos(time()/3)*3,49.5+cos(time()/4)*3
 sspr(93,90,35,38,circcdx-14,circcdy-19)
 palt()
 drawstartmnu()
 cprint(chaintxt(),63,93,14)
end

function draw_menu()
 cls()
 drawcircs() 
 mypal()
 if circtme>0 then
  sspr(93,90,35,38,circcdx-14,mnuy-69.5-(100-circtme)/5 )
 end
 palt()
 print("‹",circcx-26,mnuy+2,9)
 print("‘",circcx+21,mnuy+2,9)
 for c in all(mcards) do
  drawcard(c)
 end
 --7579
 local _flashtxt=sin(time()*3)<0 and 15 or 7
 local _mnutxtc,_mnutxty,_mnutxt={8,9,_flashtxt,_flashtxt},{24,32,43,49},{"???","payout "..vampmults[mnucur].."x","buy-in: "..vampcost[mnucur].."ML","— unlock"}

 if scoremode then
  _mnutxt,_mnutxtc[3],_mnutxty[3]={mnutxt1[mnucur],"high score:"..high[mnucur],"","— deal me in"},9,38 
 elseif unlocked[mnucur] then
		_mnutxt,_mnutxtc[3],_mnutxty[3]={mnutxt1[mnucur],mnutxt2[mnucur],"payout "..vampmults[mnucur].."x","— deal me in"},9,38
 end
 for i=1,4 do
  cprint(_mnutxt[i],64,mnuy+_mnutxty[i],_mnutxtc[i])
 end 

 if circtme==0 then
  rectfill(0,89,128,128,8)
  drawblood(89)
  if not scoremode then
	  print(chaintxt(),1,115,9)
	  print("game:"..tries,1,121,9)
  end
 else
  drawstartmnu()
  
  local bldy=circtme/100
  bldy=1-(bldy*bldy)
   
  for i=0,64 do
   local wang=i-(bldy*120-64)
   wang=(cos(mid(0.5,wang/128,0))+1)*19.5
   
   line(63-i,128-wang,63-i,128,8)
   line(64+i,128-wang,64+i,128,8)
  end 
  
  drawblood(128-39*bldy)
 end
end

function draw_score()
 local btxt=result!="pass" and "round "..result or "pass"
 
 draw_game()
 if #linet==0 then
  clip(11,winsashy,106,14)
  local perc=wint/100
  perc=1-perc*perc

  for x=0,105 do
   local wav=sin(time()+x*0.01)+1
   wav=wav*6-12+perc*30
   if wav>-1 then
    line(x+11,winsashy+13-wav,x+11,winsashy+13,8)
   end
  end
  local tperc=mid(0,(wint/50)-0.5,1)
  tperc=tperc*tperc*tperc
  bignumc(btxt,63,winsashy+2+14*tperc,0)
  clip()
 else
  if winsashy<104 then
   clip(11,0,106,104)
   rectfill(11,winsashy,116,winsashy>=90 and 103 or winsashy+13,8)
   bignumc(btxt,63,winsashy+2,0)
   clip()
  end
 end
 if winsashy<90 then
  rectfill(11,winsashy+14,116,103,9)
  clip(11,winsashy,105,105-winsashy)
  if #linet>0 then
   local nxty=winsashy+11
   for i=1,#linet do
    local nxtc,nxtx=2,21
    nxty+=6
    if linev[i]=="—" then
     nxty+=3
     nxtx,nxtc=43,sin(time())<0.5 and 10 or 2
    else
     rprint(linev[i],109,nxty,nxtc)
    end
    print(linet[i],nxtx,nxty,nxtc)    
   end
  end
  clip()
 end
end

function draw_game()
 cls()
 
 if dangrect then
  drectsani-=drectsani/60
  if drectsani<1 then
   drectsani=0
  end
  fillp(0b1010010110100101.1)
  local dr=flr(dangrect+drectsani)%4
  dr=dr*2+1
  if ldrect!=dr then
   ldrect=dr
   sfx(57)
  end
  rect(drects[dr],drects[dr+1],drects[dr]+44,drects[dr+1]+56,9)
  fillp()
 end
 for c=1,6 do
  sspr(54,21,13,10,chipx[c],chipy[c])
 end
 
 --gchips
 for i=1,6 do
  drawchip(gchips[i])
 end
 
 for c in all(cards) do
  drawcardshadow(c)
 end
 
 --shadows
 for c in all(cards) do
  drawcardshadow(c)
 end
 
 --cards
 for c in all(cards) do
  drawcard(c)
 end
 
 --chips
 for c in all(chips) do
  drawchip(c)
 end
 
 stabx+=(stabdx-stabx)/10
 staby+=(stabdy-staby)/4
 if not curstab then
  if cur==16 then
   pal(split("14,14,14,14,14,14,14,14,14,14,14,14,14,14,14"))
   for offs in all({{-1,0},{1,0},{0,-1},{0,1}}) do
    spr(32,14+stabx+offs[1],30+staby+offs[2],1,7)
   end
   pal()
  end
  spr(32,14+stabx,30+staby,1,7)
 end
 local bontxt=tostr(calcstabonus())
 local bonw=#bontxt*4+12
 local bonx,bonc=17-bonw/2+stabx,2
 if cur==16 or curstab then
  bonc=14
 end
 rrectfill(bonx,18,bonw+1,9,10)
 rectfill(bonx+1,19,bonx+bonw-1,25,0)
 
 cprint(bontxt.."ML",18+stabx,20,bonc)
 
 sepline(105)
 
 if not bonusmode and _upd==update_game then
  clip(8,98,21,8)
  rrectfill(8,98,21,10,2)
  rectfill(9,99,27,105,0)
  print("pass",11,100,cur==17 and 14 or 2)
	 clip()
  if cur==17 then
   cprint(passcost().."ML",19,92,8)
  end
 end
 
 bignumc(stakes_txt,64,108,8,true)
 
 rprint(plusminus(wining_txt)..abs(wining_txt).."ML",121,114,9)
 
 print("stakes",52,120,10)
 rprint(wining_desc,121,120,10)
 print(bonusmode and "bonus" or "round "..round,9,120,10)
 for i=1,5 do
  print(wins[i],9+(i-1)*4,114,i==round and 8 or 9)
 end
 
 mypal() 
 
 if pflockdest>0 then
  local pfspd=4
  if pflockdest!=pflocky and abs(staby-stabdy)<1 then
	  if abs(pflockdest-pflocky)<=pfspd then
	   pflockdest,shake=pflocky,10
	   sfx(56)
    for ch in all(chips) do
     ch.zd=1.5
     ch.dx+=rnd(20)-10
     ch.dy+=rnd(20)-10
    end
	  else
	   pflocky+=pfspd
	  end
  end
  clip(pflockx,0,8,pflockdest+20)
  sspr(8,25,8,28,pflockx,pflocky)
  clip()
 end

 if showcur then
	 local _curx,_cury=curx+0.5,cury+0.5
	 if curshake>0 then
	  curshake-=1
	  _curx+=sin(time()*10)
	 end
	 if curchip then
	  local cspr=chipspr[curchip.s]
		 sspr(cspr.x,cspr.y,cspr.w,cspr.h,_curx-3,_cury-6)
		 if curchip.s==1 then
		  cprint(curchip.cap,_curx+4,_cury-4,8)
		 end  
	 end
	 if curstab then
	  local stabplus=cur<=9 and 7 or 0
 	 sspr(16,31,12,13,_curx+3,_cury-stabplus)
 	else
 	 sspr(10,15,9,10,_curx,_cury)
 	end
 end
 pal()
 helpdy=talktxtd!="" and 104 or 130
 drawtxt(helpy)
end

function draw_wining() 
 cls()
 drawcircs()
 
 clip(0,52,128,40)
 for i=1,2 do
  if drplned[i]>0 then
   if dropup then
    line(62+i,89-max(drplnet[i],0),62+i,89-max(drplned[i],0),8)
   else
    line(62+i,51+max(drplnet[i],0),62+i,51+max(drplned[i],0),8)
   end
  end
 end
 if drph>0 then
  local mydrph=sin(time())+1
  mydrph=(mydrph/2+1)*drph
  if dropup then
   sspr(9,109,6,7,61,90-7*mydrph,6,7*mydrph,false,true)
  else
   sspr(9,109,6,7,61,52,6,7*mydrph)
  end
 end
 clip()
 
 sepline(51)
 clip(0,32,128,18)
 if wintxth>0.5 then
  cprint(wining_desc,64,50-wintxth,9)
  bignumc(wining_txt,64,57-wintxth,8,true)
 end
 if govertxth>0.5 then
  cprint(goverprint2,64,50-govertxth,9)
  bignumc(goverprint1,64,57-govertxth,8)
 end
 clip()
 if dropup or drpwh==0 then
  rectfill(0,89,128,128,8)
 else
	 for x=0,63 do  
	  local _myh=sin(-time()*2.5+x/10)
	  _myh=_myh*(1-min(x,1+40*drpwh)/(1+40*drpwh))
	  _myh=_myh*1.5*drpwh
	  line(63-x,89+_myh,63-x,128,8)
	  line(64+x,89+_myh,64+x,128,8)
	 end
	end
 drawblood()
end

function draw_intro()
 cls()
 
 --drawtxt(41) 
 drawtxt(helpy) 
 bignumc(blood_txt,64,113,8,true)
 
 if #talkqueue<=1 then
  if txtpicperc>0 then
   fadepal(txtpicperc,0)
  end
  sspr(100,50,28,40,50,55)
  pal()
 end
end

function draw_ask()
 cls()
 cprint("winnings",64,16,9)
 bignumc(wining_txt,64,23,8,true)
 
 drawtxt(helpy)
 
 drawbutt("yes",43,76,cur==1 and 9 or 2)
 drawbutt("no",72,76,cur==2 and 9 or 2)
 mypal()
 sspr(10,15,9,10,curx+0.5,cury+0.5)
 pal()
end

function drawtxt(y)
 local x,w=6,116
 rectfill(5,y-1,122,y+25,0)
 if txtcard then
  drawcard(txtcard)
  txtcard.y,x,w=y+12.5,26,96
 end
 
 rrectfill(x,y,w,25,10)
 rectfill(x+1,y+1,x+w-2,y+23,0)
 print(talktxt,x+4,y+4,2)
 if txtbuttx then
  rectfill(109,y+22,117,y+28,0)
  print("—",110,flr(y)+22.5+abs(sin(time()/2)),10)
 end
end

function circt(x,y,r,c,t)
 if t==1 then
  circ(x,y,r,c)
 else
  circfill(x,y,r,c)
  circfill(x,y,r-t,0)
 end
end

function drawchip(c)
 if not c then return end
 local cspr=chipspr[c.s]
 mypal()
 sspr(cspr.x,cspr.y,cspr.w,cspr.h,c.x,c.y-c.z)
 if c.s==1 then
  cprint(c.cap,c.x+7,c.y-c.z+2,8)
 end
 palt()
end

function drawcard(c)
 local cx,cy=c.x,c.y
 if not c.hidden then
  loadcard(c.val==10 and vampval or c.val)
 end
 mypal()
 if c.rot<-0.3 and c.rot>-0.5 then
  pal(0,1)
  pal(2,9)
  pal(6,7)
 end
 local sy=c.hidden and 25 or 0
 if c.rot==0 then
  sspr(108,sy,19,25,cx-9+0.5,cy-12+0.5-c.z)
 else
	 local ang=0.25+c.rot/4
	 local angx,angy=-sin(ang),cos(ang)
	 local wid=19*angx
	 
	 for x=0,18 do
	  local row,rx,ry=flr((x-9)/angx+9),cx-8+x,cy-11
	  if angx==0 then
	   row=-100
	  end
	  if row>=0 and row<=18 then
	   local h=(x-9)/-9
    h=10*h*angy
    local top=ry+h/2-c.z

	   sspr(108+row,sy,1,25,rx,top,1,25)
	  end
	 end
 end
 pal()
end

function drawcardshadow(c)
 local w=19*cos(abs(c.rot)*0.25)
 rrectfill(c.x-(w/2)+1.5,c.y-12+1.5,w,25,4)
end

function rrectfill(x,y,w,h,c)
 w=flr(w)
 if w==2 then
  rectfill(x,y,x+1,y+h-1,c)
 elseif w<=1 then
  line(x,y,x,y+h-1,c)
 else
  rectfill(x+1,y,x+w-2,y+h-1,c)
  rectfill(x,y+1,x+w-1,y+h-2,c)
 end
end

function loadcard(val)
 for i=0,24 do
  memcpy(54+i*64,0x4300+i*10+(val-2)*250,10)
 end
end

function drawblood(bldy)
 if scoremode then return end
 local bldy=bldy or 89
 palt(0,false)
 palt(8,true) 
 pal(2,0) 

 sspr(0,109,9,19,55,bldy+3)
 sspr(0,109,9,19,64,bldy+3,9,19,true)
 
 --local perc=(sin(time()/10)+1)/2
 local pxperc=1652*(blood_txt/5000)
 if pxperc>=212 then
  pxperc-=212
  pxperc=10-flr(pxperc/144)
 else
  --local lookup={4,12,48,112,212}
  local lookup,i=split("0,8,30,80,162,212"),0
  repeat
   i+=1
  until pxperc<=lookup[i]
  pxperc=i-1
  pxperc=15-pxperc
 end

 rectfill(55,bldy+2,72,bldy+2+pxperc,8)

 palt(2,true)
 pal(2,8)

 sspr(0,109,9,19,55,bldy+3)
 sspr(0,109,9,19,64,bldy+3,9,19,true)
 
 pal()
 bignumc(blood_txt,64,bldy+24,0,true)

end 

function drawcircs()
 for c in all(circs) do
  circt(c.x,c.y,c.r,c.c,c.t)
 end
end

function doshake()
	local xview,yview=rnd(shake)-shake/2,rnd(shake)-shake/2
 
 shake=max(shake-1,0)	
	if shake > 10 then
 	shake*=0.9
 end
 camera(xview,yview)
end

function drawbutt(txt,x,y,c)
 rrectfill(x,y,17,9,c)
 rectfill(x+1,y+1,x+15,y+7,0)
 cprint(txt,x+9,y+2,c)
end

function mypal()
 palt(11,true)
 palt(0,false)
end

function drawstartmnu()
 rectfill(0,99,128,128,8)
 bignum(15,103,"high stakes",0)
 
 clip(40,117,47,10)
 local fcol=sin(time()*3)<0 and 15 or 7
 for x=1,#stmnu do
  print(stmnu[x],44+(x-1)*50-stmnux,118,fcol) 
 end
 clip()
 if stcur>1 then
  print("‹",30.5-sin(time()*2),118,fcol)
 end
 if stcur<#stmnu then
  print("‘",90.5+sin(time()*2),118,fcol)
 end
 
 print(ver,2,122,9)
end

function draw_end()
 cls(0)

 clip(0,32,128,18)
 cprint(goverprint2,64,50-govertxth,9)
 bignumc(goverprint1,64,57-govertxth,8)
 clip()
 if goverprint2=="exsanguination" then
  sspr(7,69,86,39,20,58)
 else
  sspr(16,44,84,25,22,58)
 end
end

function chaintxt()
 if chain==nil or chain==0 then return "" end

 local chn="streak:"..chain
 if chain==bchain then
  chn=chn.."!"
 end
 return chn
end
-->8
--update

function update_credits()
 wint-=1
 if wint<=0 or btnp(5) or btnp(4) then 
  fadeout()
  gotostart()
 end
end

function update_start()
 mnuy,nmudy=100,31
 docircs(circcols1)
 if btnp(0) and stcur>1 then
  stcur-=1
  sfx(63)
 elseif btnp(1) and stcur<#stmnu then
  stcur+=1
  sfx(63)
 end
 stmnux+=(((stcur-1)*50)-stmnux)/5
 
 if btnp(5) then
  sfx(53)
  blood_txt=blood
  if stmnu[stcur]==" new game" then
   if blood<5000 then
    chain=0
   end
   newgame()
   setuptxtcard(10)
   blood_txt,blood,helpy,helpdy,_upd,_drw,talkqueue,txtpicperc,txtbuttx=5000,5000,-30,15.5,update_intro,draw_intro,split("an average human body\ncontains about 5000ML\nof blood|heheheh...|oh no! where did all\nthe blood go?!\nheheheh...|don't take it too hard\nyou were living on\nborrowed time anyway|here, the last shot is\non me. it will take\nthe edge off|or maybe you can win\nit all back? how about\nit? one last game...","|"),1,true   nexttalk()
   fadeout()
  else
   scoremode=stmnu[stcur]=="score mode"
   _upd,_drw,circtme=update_menu,draw_menu,100
   
  end
  
 end
end

function update_menu()
 update_gtxt()
 circtme,vampval=max(circtme-2.5,0),10
 local circfirst=true
 for i=1,#circs do
  if i/#circs>(circtme)/100 then 
   if circfirst and circtme>0 then
    circfirst,circs[i].carr=false,circcols3
   else
    circs[i].carr=circcols2
   end
  end
 end

 docircs(circcols2)
 
 if btnp(0) then
  mnucur-=1
  sfx(63)
 elseif btnp(1) then
  mnucur+=1
  sfx(63)
 end
 mnucur=mid(mnucur,1,4)
 mnudx=50*(mnucur-1)
 mnux+=(mnudx-mnux)/10
 mnuy+=(mnudy-mnuy)/20

 for i=1,4 do
  local randx,randy=cos((time()+i*0.79)/2)*1.7,cos((time()+i*0.79)/3)*1.7
  mcards[i].x=64-mnux+(i-1)*50+randx
  mcards[i].y=mnuy+cos((mcards[i].x-64)/160)*5+randy
 end
 circcdx,circcdy=mcards[mnucur].x,mnuy+2
 
 if btnp(5) then
  if unlocked[mnucur] or scoremode then
	  sfx(53)
	  fadeout()
   vampval,mult,dropboxes,droptoken,blood_txt,_upd,_drw=9+mnucur,vampmults[mnucur],mnucur>1,vamptoken[mnucur],blood,update_game,draw_game
   startmatch()
	 else
	  if blood>vampcost[mnucur] then
	   sfx(53)
    mult,ticksfx,blood,unlocked[mnucur]=vampmults[mnucur],54,blood-vampcost[mnucur],true
	   savegame()
	  else
	   sfx(61)
	  end
	 end
 end
end

function update_game()
 update_gtxt()
 dotxt()
 if wining_txt==wining and ticksfx and not bonusmode then
  ticksfx=nil
 end
 local noani,canact=true,true
 for c in all(cards) do
  if c.delay>0 then
   c.delay-=1
  else
   c.x+=(c.dx-c.x)/5
   c.y+=(c.dy-c.y)/5
   c.z+=c.zd
   c.zd-=0.15
   if c.z<=c.zbase then
    c.z,c.zd=c.zbase,0
   end
   if abs(c.x-c.dx)<0.3 then
    c.x=c.dx
   end
   if abs(c.y-c.dy)<0.3 then
    c.y=c.dy   
   end
   
  end
  if c.ani then
   canact,noani=canact and c.ani(c),false
  end
  if c.x!=c.dx or c.y!=c.dy then
   canact=false
  end
 end
 if canact and autoflip then
  autoflip.hidden,canact=false,false
  dodroptoken()
  autoflip.hidden=true
  flipbegin(autoflip)
  autoflip=nil
 end
 
 dochips()
 
 if pflockdest>0 then
  canact,showcur=false,false
  if pflockdest==pflocky and pflockard then
   if pflockard==vcard then
    pflockard=nil
    endgame("stab")
   else
    flipbegin(vcard)
    vcard.badstab,pflockard=true,nil
   end
  end
 end
 
 if result!="" then
  if noani and pflockdest==pflocky then
   --–
   wait(20,false,true)
   if result=="won" then
    sfx(41,3)
   end
   wait(50,false,true)
		 winsashy,winsashdy,wint,wintstep,_upd,_drw=90,90,100,60,update_score,draw_score
  end
  return 
 end

 local oldcur=cur
 if showcur then
	 if btnp(0) then
	  cur=curpos[cur].lf
	 elseif btnp(1) then
	  cur=curpos[cur].rt
	 elseif btnp(2) then
	  cur=curpos[cur].up 
	 elseif btnp(3) then
	  cur=curpos[cur].dw
	 end
 end
 if stabonus<=0 and cur==16 then
  cur=oldcur
 end
 if curchip and cur>=16 then
  cur=oldcur
 end
 if bonusmode and cur>=16 then
  cur=oldcur
 end
 if curstab and (cur>16 or (cur>9 and cur<16)) then
  cur=oldcur
 end
 if oldcur!=cur then
  sfx(63)
  for c in all(cards) do
   c.zbase=0
   for c2 in all(c.chips) do
    c2.zbase=0
   end
  end
  for c in all(gchips) do
   c.zbase=0
  end
  if curchip==nil then
	  if cur<=9 and cards[cur].hidden then
	   cards[cur].zbase=1
	   for c2 in all(cards[cur].chips) do
	    c2.zbase=1
	   end
	  elseif cur<=16 then   
	   local c=gchips[cur-9]
	   if c and (c.s==1 or c.s==3 or c.s==10) then
	    c.zbase=1
	   end
	  end
  end
 end
 if canact and result=="" then
  showcur=showcur or canact
  dobutts()
 end
 
 local chipbst=0
 if cur<=9 and curchip then
  chipbst=7
 end
 curx+=(curpos[cur].x-curx)/1.5
 cury+=(curpos[cur].y-chipbst-cury)/1.5
 
 --‡
 local helpi=0
 if canact and showcur then
		if curchip or (cur>=10 and cur<=15) then
		 gch=curchip
		 if gch==nil then 
		  gch=gchips[cur-9]
		 end
		 if gch then
		  chs=gch.s
		  if chs==1 then
		   helpi=1
		  elseif chs==3 then
		   helpi=2
		  elseif chs==9 then
		   helpi=6
		  elseif chs!=9 then
		   if cur<=12 then
		    helpi=4
		   else
		    helpi=5
		   end
		  end
		 end
		elseif cur==16 or curstab then
		 helpi=3
		end
 end
 talktxt=helpi==0 and "" or helptxt[helpi]
 talktxtd=talktxt
end

function dobutts()
 if btnp(5) then
  if cur<=9 then
   local c=cards[cur]
   if curchip then
    if curchip.s==1 then
     if c.hidden and not c.had1 then
      dropchip1(curchip,c)
      del(gchips,curchip)
      curchip=nil
      sfx(60)
     else
      sfx(61)
      curshake=10
     end
    elseif curchip.s==3 then
     if not c.hidden and not c.had3 and hasouts(cur) then
      dropchip3(curchip,c)     
      del(gchips,curchip)
      curchip=nil
      sfx(60)
     else
      sfx(61)
      curshake=10
     end
    end
   elseif curstab then
    if c.hidden then
     --stab
     stabcard(c)
     pflockard=c
    else
     sfx(61)
     curshake=10    
    end
   else
    --normal flip
	   if c.hidden then
	    flipbegin(c)
	   else
	    if c.ani==nil then
	     if c.zd==0 then
	      c.zd=0.8
	     else
	      c.zd=-c.zd/2
	     end
	    end
	   end
	  end
  elseif cur<16 then
   if curchip then
    returnchip()
   elseif curstab then
    curstab=false
    sfx(59)  
   else
	   local c=gchips[cur-9]
	   if c then
	    if c.s==1 or c.s==3 then
		    --trigger
      curchip,curchipi,gchips[cur-9]=c,cur-9,nil
		    sfx(60)
	    elseif c.s==9 then
	     dropchip9()
	     gchips[cur-9]=nil
	     sfx(60)
	    end
	   end
	  end
  elseif cur==16 then
   if curchip then
    returnchip()
   elseif curstab then
    curstab=false
    sfx(59)   
   else
    curstab=true
    sfx(60)
   end
  elseif cur==17 then  
   if curchip then
    returnchip()
   elseif curstab then
    curstab=false
    sfx(59)    
   else
    -- stake
    endgame("pass")
    sfx(51)
   end  
  end
 elseif btnp(4) then
  if curchip then
   returnchip()
  elseif curstab then
   curstab=false
   sfx(59)
  end
 end
end

function update_score()
 ticksfx=55
 update_gtxt()
 wint-=1
 winsashy+=(winsashdy-winsashy)/10
 dochips()
 if wint<=0 then
  if #linet_next>0 then
   local nt,nv=linet_next[1],linev_next[1]
   add(linet,nt)
   add(linev,nv)
   del(linet_next,nt)
   del(linev_next,nv)
   winsashdy-=6
   if nv=="—" then
    winsashdy-=2
    tempsave()
   end
   if #linet==1 then
    winsashdy-=5
   end
   if #linet>1 and nv!="—" then
    stakes+=tonum(sub(nv,1,-3))
   end
   wint=wintstep
  end
 end
 
 if btnp(5) or btnp(4) then
  wintstep,wint=5,0
  if #linet_next==0 then
   winstakes()
   --’
   if round>=5 then
    if bonusmode and result=="won" then wbin=1 end
    repeat
     flip()
     frames+=1
     update_gtxt()
     winsashdy=105
     winsashy+=(winsashdy-winsashy)/5
     _draw()
    until stakes_txt==stakes
    local _mywins=0
    for w in all(wins) do
     if w=="w" then _mywins+=1 end
    end
    if _mywins>=3 and not bonusmode then
     gotoask()
    else
     gotowin()
    end
   else
    dealround()
    _upd,_drw=update_game,draw_game
   end
  end
 end
end

function update_wining()
 ticksfx,showcur,circcdy,circcdx=54,false,40,64
 update_gtxt()
 docircs(circcols2)
 wint-=1
 wintxth+=(wintxtdh-wintxth)/10
 if winphase==0 then
  wintxtdh=18
	 if wint<=0 then
	  if not scoremode then
 	  blood=mid(blood+wining,0,5000)
 	 end
   savegame()
   if scoremode then
    winphase=4
	  elseif wining==0 then
	   winphase=3
	  else
 	  wining,winphase=0,1
	   drplned[1]=-rnd(90)
	  end
	 end 
 elseif winphase==1 then
  for i=1,2 do
   drplnedd[i]+=0.05
  end
  drph+=(1-drph)/50
  if wining_txt==0 then
   winphase,drplnet[2]=2,-rnd(90)
  end
 elseif winphase==2 then
  for i=1,2 do
   drplnedd[i]+=0.05
   drplnetd[i]+=0.05
  end
  drph+=-drph/10
  if drplnet[1]>90 and drplnet[2]>90 then
   winphase=3
  end
 elseif winphase==3 then
  if blood<=0 then
   winphase,wint,goverprint2,goverprint1,chain=5,1000,"exsanguination","you died",0
   music(9)
  elseif blood>=5000 then
   winphase,wint,goverprint2,goverprint1=5,1000,"congratulations","you won"
   chain+=1
   bchain=max(chain,bchain)
   music(10)
  else
   winphase,wint=4,200
  end
 elseif winphase==4 then
  if wint<=0 then
   wintxtdh=0
  end
  if wintxth<0.5 then
   wintxth,mnuy,mnudy,_upd,_drw=0,-60,31,update_menu,draw_menu
   music(0)
   _upd()  
  end
 elseif winphase==5 then
  wintxtdh,govertxtdh=0,18
  if wintxth<0.9 then
   wintxth=0
   _draw()
   fadeout()
   _upd,_drw=update_end,draw_end
  end
 end
 
 if winphase>=1 then
  local raisewav,lowerwav=false,false
  for i=1,2 do
   drplned[i]+=drplnedd[i]
   drplnet[i]+=drplnetd[i]
   if drplned[i]>=89 then
    raisewav=true
   end
   if drplnet[i]>0 then
    lowerwav=true
   end
  end 
  if lowerwav then
   drpwh=max(0,drpwh-0.02)
  elseif raisewav then
   drpwh=min(1,drpwh+0.01)
  end
 end
 if btnp(5) or btnp(4) then
  wint,wintxth,govertxth,blood_txt,wining_txt=0,wintxtdh,govertxtdh,blood,wining  
 end
end

function update_intro()
 txtpicperc=max(0,txtpicperc-0.03)
 update_gtxt()
 dotxt()
 if btnp(5) then
  if #talkqueue>0 then
   nexttalk()
   if #talkqueue==4 then
    blood,mult,ticksfx=0,187,54
   elseif #talkqueue==1 then
    txtpicperc,blood,blood_txt,mult=1,20,0,1
   end
  else
   sfx(51)
   fadeout()
   _upd,_drw,circtme=update_menu,draw_menu,0
  end
 end
end

function update_ask()
 helpdy=41
 dotxt()
  
 if btnp(0) then
  cur=1
  sfx(63)
 elseif btnp(1) then
  cur=2
  sfx(63)
 end
 local curdx=cur==1 and 48 or 77

 curx+=(curdx-curx)/1.5
 cury=83
 
 if btnp(5) then
  --sfx
  sfx(51)
  if cur==2 then
   gotowin()
  else
   fadeout()
   dealround()
   _upd,_drw,bonusmode,stakes,stabdx=update_game,draw_game,true,wining,-30
   musiclvl(3)
  end
 end
end

function dotxt()
 helpy+=(helpdy-helpy)/4
 if frames%2==0 then
	 if #talktxt!=#talktxtd then
	  talktxt=sub(talktxtd,1,#talktxt+1)
	  sfx(52)
	 end
 end
end

function nexttalk()
 local t=talkqueue[1]
 talktxtd,talktxt=t,""
 del(talkqueue,t)
end

function docircs(colarr)
 circcx+=msgn(circcdx-circcx)*0.5
 circcy+=msgn(circcdy-circcy)*0.5
 if abs(circcdy-circcy)<0.5 then
  circcy=circcdy
 end
 if abs(circcdx-circcx)<0.5 then
  circcx=circcdx
 end
 
 for c in all(circs) do
  c.r+=0.26
  if c.r>=90 then 
   c.r,c.t,c.carr=0,1,colarr
   del(circs,c)
   add(circs,c)
  end
  local myage=c.r/90
  c.x,c.y=64.5*myage+circcx*(1-myage),49.5*myage+circcy*(1-myage)

  for cc in all(c.carr) do
   if c.r<cc[1] then
    c.c=cc[2]
   end
  end
  
  if c.r>71 then
   c.t=4
  elseif c.r>55 then
   c.t=3
  elseif c.r>40 then
   c.t=2
  end
 end
end

function update_gtxt()
 if frames%5==0 then
		if stakes_txt!=stakes then
		 if abs(stakes-stakes_txt)<mult then
		  stakes_txt=stakes
		 else
 		 stakes_txt+=sgn(stakes-stakes_txt)*mult
 		end
 	 if ticksfx then
	 	 sfx(ticksfx)
		 end
		end
		if wining_txt!=wining then
		 if abs(wining-wining_txt)<mult then
		  wining_txt=wining
		 else
 		 wining_txt+=sgn(wining-wining_txt)*mult
		 end
 	 if wining_desc=="winnings" and wining_txt<0 then
	   wining_desc="losses"
	  end
		 if wining_desc=="losses" and wining_txt>0 then
 	  wining_desc="winnings"
	  end
		end
		if blood_txt!=blood then
		 if abs(blood-blood_txt)<mult then
		  blood_txt=blood
		 else
 		 blood_txt+=sgn(blood-blood_txt)*mult
 		end
		 if ticksfx then
 		 sfx(ticksfx)
 		end
		end

	end
end

function ani_flip(crd)
 crd.rot+=0.1
 if crd.rot>=1 then
  crd.hidden,crd.rot=false,-1
  cardflip(crd)
 elseif crd.rot>=0 and not crd.hidden then
  crd.rot,crd.ani=0,nil
 end
 return crd.rot<=0
end

function dochips()
 for c in all(gchips) do
  dochip(c)
 end
 for c in all(chips) do
  dochip(c)
 end
end

function dochip(c)
 if c.delay>0 then
  c.delay-=1
 else
  if c.dx and abs(c.dx-c.x)>=1 then
   c.x+=sgn(c.dx-c.x)*1
  end
  if c.dy and abs(c.dy-c.y)>=1 then
   c.y+=sgn(c.dy-c.y)*1
  end

  c.z+=c.zd
  c.zd-=0.25
  if c.z<=c.zbase then
   c.z=c.zbase
   if abs(c.zd)>1.5 then
    c.zd=-c.zd*0.3
    sfx(58)
   else
    c.zd=0
   end
  end
  if c.delme and c.y-c.z<=-10 or c.x<=-14 then
   del(chips,c)
   del(gchips,c)
  end
 end
end

function update_end()
 govertxth+=(govertxtdh-govertxth)/10
 wint-=1
 if wint<=0 or btnp(5) then
  fadeout(0.01)
  savegame() 
  gotostart()
  makecircs()
  wait(120,true)
  music(0,2000) 
 end
end
-->8
--gameplay
function gotostart()
 _upd,_drw=update_start,draw_start
 stmnu,stmnux,stcur=split(" continue, new game,score mode"),0,1
 if blood<=0 or blood>=5000 then
  del(stmnu," continue")
 end
 --bchain=1
 if bchain==0 then
  del(stmnu,"score mode")
 end

end

function gotowin()
 fadeout()
 music(-1,4000)
 high[mnucur]=max(high[mnucur],wining)
 wint,winphase,drplnet,drplned,drplnetd,drplnedd,drph,drpwh,wintxth,wintxtdh,govertxth,govertxtdh,dropup,_upd,_drw=200,0,{0,0},{0,0},{0,0},{0,0},0,0,0,0,0,0,wining<0,update_wining,draw_wining
end

function gotoask()
 setuptxtcard(10)
 fadeout()
 talktxt,talktxtd,cur,txtbuttx,helpy,helpdy,_upd,_drw="","how about a final\nbonus round? double\nwinnings or nothing?",1,false,-30,41,update_ask,draw_ask
end

function setuptxtcard(val)
 txtcard={
  x=15,
  y=54,
  z=1,
  zbase=1,
  val=val,
  hidden=false,
  rot=0,
  chips={}
 }
end

function startmatch()
 genbacks()
 musiclvl(0)
 music(1) 
 round,wining,wining_txt,wining_desc,wins,stakes_txt,stabx,staby=0,0,0,"winnings",split(".,.,.,.,."),0,-20,0
 dealround()
end

function dealround()
 talktxtd,txtcard,helpy,txtbuttx,bonusmode,stabdx,stabdy,stabonus,pflockx,pflocky,pflockdest,pflockard,curchip,curstab,showcur,cards,dangrect="",nil,130,false,false,0,0,7,0,-28,-28,nil,nil,false,false,{},nil
 if staby!=0 then
  stabx,staby=-20,0
 end
 musiclvl(0)
 
 local cardvals={2,3,4,5,6,7,8,9,10}
 for i=1,9 do
  local thisval=rnd(cardvals)
  del(cardvals,thisval)
  add(cards,{
   pos=i,
   dx=cardx[i]+0.5,
   dy=cardy[i]+0.5,
   x=54,
   y=-70,
   z=10,
   zd=0,
   zbase=0,
   val=thisval,
   delay=10*i,
   hidden=true,
   rot=0,
   chips={},
   hint=0,
   row=flr((i-1)/3)
  })
  if thisval==10 then
   vcard=cards[#cards]
  end
 end
  
 repeat
  autoflip=rnd(cards)
 until autoflip.val!=10
 autoflip.hidden,cur=false,autoflip.pos
 
 local cps={9,8,7,6,5,4,3,2}
 if vampval==13 then
  --asshole mode
  vcard.hint=autoflip.val
  del(cps,autoflip.val) 
 end
 
 for h in all(cps) do
  local targs={}
  for i=1,#cards do
   local crd=cards[i]
   if crd.hidden and crd.val>=h and crd.hint==0 then
    add(targs,crd)
   end
  end
  
  if #targs>0 then
   rnd(targs).hint=h
  end  
 end 

 autoflip.hidden,gchips,chips=true,{},{}

 for i=1,6 do
  add(gchips,{
   x=chipx[i],
   y=chipy[i],
   z=0,
   zd=0,
   zbase=0,
   s=rnd({2,4}),
   delay=0,
   cap="+"
  })
 end
 if dropboxes then
  if vampval==13 then
   --asshole mode
   local cands,vcands={},{}
   for c in all(gchips) do
    add(cands,c)
   end
   add(vcands,gchips[(vcard.pos-1)%3+1])
   add(vcands,gchips[vcard.row+4])
   for c in all(vcands) do
    del(cands,c)
   end
   rnd(rnd(5)<3 and vcands or cands).s=10
  else
   rnd(gchips).s=10
  end
 end
  
 result,stakes="",0
 round+=1
end

function flipbegin(crd)
 swipechips(crd)
 crd.ani,crd.zd=ani_flip,1.4
 sfx(62)
end

function cardflip(crd)
 if crd.badstab then
  endgame("badstab")
  return
 elseif not bonusmode then
	 st_raise(1)
	 stabonus-=1
	 if stabonus<=0 then
	  stabdx=-30
	 end
 end
 local hcount=hidcount()
 for chii,chi in pairs(split2d("1,4,7|2,5,8|3,6,9|1,2,3|4,5,6|7,8,9")) do
  local chic=0
  for chj in all(chi) do
   if cards[chj].hidden==false then
    chic+=1
   end
  end
  if chic==3 then
   local mychip=gchips[chii]
   if mychip and (mychip.s==2 or mychip.s==2 or mychip.s==4 or gchips[chii].s==10) then
    mychip.zd=1.5
    mychip.s-=1
   end
  end
 end 
 if crd.val==10 then
  endgame("lose")
 elseif hcount==1 then
  endgame("win")
 else
  --0x3100
  if not bonusmode then
	  if hcount<=3 then
	   musiclvl(3)
	  elseif hcount<=5 then
	   musiclvl(2)
	  elseif hcount<=7 then
	   musiclvl(1)
	  end
  end
 end
end

function stabcard(c)
 swipechips(c)
 stabdy,pflockdest,pflockx=-128,c.y-17,c.x-2
end

function st_raise(v)
 stakes+=mult*v
end

function hidcount()
 local ret=0
 for c in all(cards) do
  if c.hidden or c.badstab then
   ret+=1
  end
 end
 return ret
end

function winstakes()
 if result=="lost" then
  stakes=-stakes
 end
 wining+=stakes
 stakes=0
end

function hasouts(ci)
 local dsts={cards[ci-1],cards[ci+1],cards[ci-3],cards[ci+3]}
 for i=1,4 do
  if dsts[i] and dsts[i].hidden and(i>2 or dsts[i].row==cards[ci].row) then
   return true
  end
 end
 return false
end

function dodroptoken()
 local cand={}
 for i=1,9 do
  if cards[i].hidden then
   add(cand,i)
  end
 end
 
 for i=1,droptoken do
  local drp=rnd(cand)
  local chp={
   x=0,
   y=0,
   zbase=0,
   s=1,
   delay=(i-1)*10
  }
  dropchip1(chp,cards[drp])
  chp.z=chp.y+12
  chp.zd=0
  del(cand,drp)
 end
end


function dropchip1(chip,card)
 add(chips,chip)
 chip.x,chip.y,chip.zd,chip.cap,card.had1=card.x-5,card.y-3,2,card.hint.."+",true
 chip.dx,chip.dy=chip.x,chip.y

 add(card.chips,chip)
end

function dropchip3(chip,card)
 card.had3=true
 local pos=card.pos
 local _newx,_newy,_news,_newb,_newdst=split("-8,14,2,2"),split("0,0,-14,14"),split("5,6,7,8"),split("6,5,8,7"),{cards[pos-1],cards[pos+1],cards[pos-3],cards[pos+3]}
 for i=1,4 do
  if _newdst[i] and _newdst[i].hidden and (i>2 or _newdst[i].row==card.row) then
   local _newc={
    x=card.x-5,
    y=card.y-3,
    zd=2,
    z=0,
    zbase=0,
    s=_newdst[i].val<card.val and _news[i] or _newb[i],
    delay=0
   }
   _newc.dx,_newc.dy=_newc.x+_newx[i],_newc.y+_newy[i]
   
   add(chips,_newc)
   add(card.chips,_newc)
   add(_newdst[i].chips,_newc)
  end
 end
end

function dropchip9()
 local bxs,bestc,best=split2d("1,2,4,5|2,3,5,6|5,6,8,9|4,5,7,8"),-1,-1
 for i=1,4 do
  local ten,hid=false,0
  for j in all(bxs[i]) do
   ten=ten or cards[j].val==10
   if cards[j].hidden then
    hid+=1
   end
  end
  if ten then
   if hid+rnd()>best then
    best,bestc=hid+rnd(),i
   end
  end
 end
 dangrect,drectsani,ldrect=bestc-1,20,5
end

function calcstabonus()
 local ret=stabonus+hidcount()-1 
 return ret*mult
end

function swipechips(c)
 if c.chips then
	 for cc in all(c.chips) do
	  cc.zd,cc.delme=8,true
	  sfx(58)
	 end
 end
end

function scoreline(cap,val)
 add(linet_next,cap)
 add(linev_next,val)
end

function endgame(how)
 tempsave()
 musiclvl(0)
 result,wins[round],showcur,linet_next,linev_next,linet,linev="won","w",false,{},{},{},{}
	     
 local crds=9-hidcount()
 if not bonusmode then
  scoreline("flipped "..crds.." cards",crds*mult.."ML")
 end

 if how=="win" then 
  stabcard(vcard)
  if bonusmode then
   scoreline("winnings doubled","")
  else
   scoreline("vampire evaded",2*mult.."ML")
  end 
 elseif how=="lose" then
  result,wins[round],shake="lost","l",2
  sfx(40,3)
  if bonusmode then
   scoreline("winnings lost","")
  end 

 elseif how=="pass" then
	 result,wins[round]="pass","p"
  scoreline("pass penalty",passcost().."ML")
  	 
 elseif how=="stab" then	
  scoreline("vampire evaded",2*mult.."ML")
  scoreline("stab bonus",calcstabonus().."ML")
   
 elseif how=="badstab" then
	 result,wins[round]="lost","l"
	 scoreline("stab penalty",calcstabonus().."ML")
	 sfx(40,3)
 end
 
 scoreline("— continue","—")
end

function returnchip()
 gchips[curchipi],curchip.zd=curchip,0.8
 curchip=nil
 sfx(59)
end

function passcost()
 local cst=scoremode and -2 or -2-chain
 return cst*mult
end
-->8
--tools

function plusminus(v)
 if v<0 then
  return "-"
 elseif v>0 then
  return "+"
 end
 return ""
end

function dofade()
 fadeperc=min(fadeperc,1)
 fadepal(fadeperc,1)
end

function fadepal(perc,plt)
 for c=0,15 do
  pal(c,fadetable[c+1][flr(perc*16+1)],plt)
 end
end

function wait(_wait,_skipdraw,_dochips)
 repeat
  _wait-=1
  if not _skipdraw then
   if _dochips then
    dochips()
   end
   _draw()
  end
  flip()
 until _wait<0
end

function fadeout(spd,_wait)
 local spd,_wait=spd or 0.04,_wait or 0
 repeat
  fadeperc=min(fadeperc+spd,1)
  dofade()
  flip()
 until fadeperc==1
 wait(_wait)
end

function split2d(s)
 local arr=split(s,"|")
 for i=1,#arr do
  arr[i]=split(arr[i])
 end
 return arr
end

function bignumc(num,tx,ty,c,ml)
 local stklen=bignumlen(num)
 bignum(tx-stklen/2,ty,num,c)
 if ml then
  print("ML",tx+stklen/2+2,ty+5,c)
 end
end

function bignumlen(num)
 local snum,w=tostr(num),0
 for i=1,#snum do
  local cs=sub(snum,i,i)
  local bb=bignums[cs]
  if bb then
   w+=bb.w+bb.kern*2+2
  else
   w+=6
  end
 end
 return w-2
end

function bignum(x,y,num,col)
 local snum,px,shrt,w,col=tostr(num),x,6,8,col or 7
 
 pal(7,col)
  
 for i=1,#snum do
  local cs=sub(snum,i,i)
  local bb=bignums[cs]
  
  if bb then
   local alt=bb.alt
	  if alt then
	   pal(5,col)
	   palt(6,true)
	   shrt=4
	  else
	   palt(5,true)
	   pal(6,col)
	  end
	  w=bb.w+bb.kern*2
   sspr(bb.x,bb.y,bb.w,10,px+bb.kern,y)
  elseif cs=="-" then
   rectfill(px,y+4,px+3,y+5,col)
   w=4
  else
   w=4
  end
  px+=w+2
 end
 pal()
end

function sepline(y)
	line(8,y,119,y,2)
	sspr(0,108,6,1,8,y)
 sspr(0,108,6,1,114,y,6,1,true)
end

function rprint(txt,x,y,c)
 local rx=x-#tostr(txt)*4
 print(txt,rx,y,c)
end

function cprint(txt,x,y,c)
 local rx=x-#tostr(txt)*2
 print(txt,rx,y,c)
end

function msgn(v)
 return v==0 and 0 or sgn(v)
end
-->8
--music
function musiclvl(lvl)
 for i=1,8 do
  for j=1,3 do
   if j<lvl+1 then
    --0 play
    poke(0x3100+4*i+j,peek(0x3100+4*i+j)&0b10111111)
   else
    --1 no play
    poke(0x3100+4*i+j,peek(0x3100+4*i+j)|0b01000000)
   end
  end
 end

end
-->8
--cdata
function newgame()
 tries+=1
 blood,unlocked=20,{true,false,false,false}
 savegame()
end

function tempsave()
 if scoremode then return end
 local tblood=blood
 blood=mid(blood+wining,0,5000)
 savegame()
 blood=tblood
end

function savegame()
 dset(0,1)
 dset(1,blood)
 dset(2,tries)
 dset(6,chain)
 dset(7,bchain)
 dset(12,wbin)
 for i=2,4 do
  dset(i+1,unlocked[i] and 1 or 0)
 end
 for i=1,4 do
  dset(i+7,high[i])
 end
 dogpio()
end

function loadgame()
 unlocked,high={true},{}
 if dget(0)==1 then
  blood,tries,chain,bchain,wbin=dget(1),dget(2),dget(6),dget(7),dget(12)
  for i=2,4 do
   unlocked[i]=dget(i+1)==1
  end
  for i=1,4 do
   high[i]=dget(i+7)
  end
  dogpio() 
 else
  blood,tries,chain,bchain,high,wbin=0,0,0,0,split("0,0,0,0"),0
  savegame()
 end
end

function dogpio()
 poke(0x5f95,bchain)
 poke(0x5f96,unlocked[2] and 1 or 0)
 poke(0x5f97,unlocked[3] and 1 or 0)
 poke(0x5f98,unlocked[4] and 1 or 0)
 poke(0x5f99,wbin)
end