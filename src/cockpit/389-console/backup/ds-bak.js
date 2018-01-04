//var DS_HOME = "/etc/dirsrv/"; 
var server_id = "None";

function loadServers() {
    var loc = "servers.html";
    document.getElementById('mainFrame').setAttribute('src', loc);
    console.log("Loaded");
}

function search_dse(){
    document.getElementById('results').innerHTML = "Searching root dse...";

    var cmd = ['/usr/bin/ldapsearch','-Y', 'EXTERNAL' , '-H',
                   'ldapi://%2fvar%2frun%2fslapd-localhost.socket',
                   '-b' ,'' ,'-s', 'base', 'objectclass=top'];
    cockpit.spawn(cmd, { superuser: true }).done(function(data) {
       $("#results").text(data); }
    );
}

function setup() {
    $("#servers").on("click", loadServers);
    $("#search").on("click", search_dse); 
}

function get_insts() {
    var insts = ["None"];
    var DS_HOME = "/etc/dirsrv/";
    var cmd = ["/bin/sh", "-c", "/usr/bin/ls -d " + DS_HOME + "slapd-*"];

    cockpit.spawn(['ls', '/tmp'], { superuser: true }).done(function(data) {
        insts= ["phase1"];
    }).fail(function(error){ 
        insts = ["FAIL"];
    });

    cockpit.spawn(cmd, { superuser: true }).done(function(data) {
       var lines = data.split('\n');
       var new_insts = [];
       for (var i = 0; i < lines.length; i++) {
           if (lines[i].endsWith(".removed") == false) {
               var server_id = lines[i].replace(DS_HOME, "");
               new_insts.push(server_id);
           }
       } 
       insts = new_insts;
    }).fail(function(error){
       // maybe do a pop up dialog?
       insts = ["error"];
    });
    
    // Populate the server instance drop down
    var select = document.getElementById("select-server");    
    for(var i = 0; i < insts.length; i++) {
        var opt = insts[i];
        var el = document.createElement("option");
        el.textContent = opt;
        el.value = opt;
        select.appendChild(el);
    }
    select.selectedIndex = "0";  // Select the first instance
}

$(window.document).ready(function() {
    setup();
    get_insts();    
    $("body").show();
});


