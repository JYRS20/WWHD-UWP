// Regression guard: UWP-specific branches must not disable desktop host behaviour.
#include "platform/host.h"
#include "platform/process.h"
#include <stdexcept>
#include <iostream>
int main(int argc,char** argv) {
 if(argc==2 && !strcmp(argv[1],"--child")){std::cout<<"desktop-child-output";return 7;}
 try {
    auto executable=host::executable_path();
    if(executable.empty() || !host::executable_base())throw std::runtime_error("desktop executable lookup failed");
    auto child=host::run_process({executable,"--child"});
    if(child.code!=7 || child.output!="desktop-child-output" || !child.error.empty())throw std::runtime_error("desktop child process changed behaviour");
    host::set_thread_name("desktop-check");char name[64];host::get_thread_name(name,sizeof name);
    if(strcmp(name,"desktop-check"))throw std::runtime_error("desktop thread name changed");
    _putenv_s("WWHD_USER_DIR","desktop-test-override");
    if(host::config_dir()!="desktop-test-override")throw std::runtime_error("desktop user-dir override changed");
    std::cout<<"Desktop process execution, module lookup, thread name and configuration override PASS\n";return 0;
 }catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}
}
