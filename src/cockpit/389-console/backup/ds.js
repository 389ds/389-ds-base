var DS_HOME = "/etc/dirsrv/"; 
var server_id = "None";


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
    $("#search").on("click", search_dse); 
    $("#getservers").on("click", get_insts);
}

function get_insts() {
    var insts = [];
    var cmd = ["/bin/sh", "-c", "/usr/bin/ls -d " + DS_HOME + "slapd-*"];

    cockpit.spawn(cmd, { superuser: true }).done(function(data) {
       // Parse the output, and skip removed instances and empty lines
       var lines = data.split('\n');
       for (var i = 0; i < lines.length; i++) {
           if (lines[i].endsWith(".removed") == false && lines[i] != "") {
               var server_id = lines[i].replace(DS_HOME, "");
               insts.push(server_id);
           }
       } 

       // Populate the server instance drop down
       var select = document.getElementById("select-server");
       for(var i = 0; i < insts.length; i++) {
           var opt = insts[i];
           var el = document.createElement("option");
           el.textContent = opt;
           el.value = opt;
           select.appendChild(el);
       }
       select.selectedIndex = "0";
       server_id = insts[0];
    }).fail(function(error){
        var select = document.getElementById("select-server");
        var el = document.createElement("option");
        el.textContent = "No instances";
        el.value = "No instances";
        select.appendChild(el);
        select.selectedIndex = "0";
    });
}

$(function() {
    $('#select-server').change(function() {
        server_id = $(this).val();
    });
});

$(window.document).ready(function() {
    setup();
    get_insts();    
    $("body").show();
});


