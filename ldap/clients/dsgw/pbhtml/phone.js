//
// PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
// license terms. Copyright © 2001 Sun Microsystems, Inc.
// Some preexisting portions Copyright © 2001 Netscape Communications Corp.
// all rights reserved.
//
function goToURL(i){
window.location.href=i;
}

function easter(){
if (document.forms[0].searchstring.value=='worker and parasite'){
    window.open ("worker.qt","worker","scrollbars=no,menubar=no,resizable=no,width=300,height=300");
               }
}

function flipImg(currImg,newImg) {
        document.images[currImg].src = newImg;
}

function phoneTeam (){

 window.open ("team.html","rah_team","scrollbars=no,menubar=no,resizable=yes,width=500,height=500");

}

function fieldFocus(){
setTimeout("document.forms[0].searchstring.focus()",400);
}

function checkForNullString(){
if (document.forms[0].searchstring.value != "")
  {
    return true;
  }
else
  {
    parent.resultframe.location="/dsgw/bin/lang?context=pb&file=nullStringError.html";
    return false;
  }
}


