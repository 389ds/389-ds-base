function get_insts() {
    var insts = ["None"];

    cockpit.spawn(['ls', '/tmp'], { superuser: true }).done(function(data) {
        insts= ["okay we actually here"];
    }).always(function(){
       insts = ['always'];
    }).fail(function(error){ 
        insts = ["FAIL"];
    });

    // Populate the server instance drop down
    var select = document.getElementById("select-server");    
    var opt = insts[0];
    var el = document.createElement("option");
    el.textContent = opt;
    el.value = opt;
    select.appendChild(el);
}

$(window.document).ready(function() {
    get_insts();    
    $("body").show();
});


