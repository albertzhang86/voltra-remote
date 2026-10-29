#pragma once
#include <cstdint>

namespace remote {
// Display intent only. Never use this to grant control or infer device readiness.
enum class SetupActivity:uint8_t {None,Connecting,CreateTwin,ExitTwin,Configure,Load};
inline const char* setupActivityLabel(SetupActivity activity){
 switch(activity){
 case SetupActivity::Connecting:return "Connecting";
 case SetupActivity::CreateTwin:return "Creating Twin";
 case SetupActivity::ExitTwin:return "Exiting Twin";
 case SetupActivity::Configure:return "Setting up VOLTRA";
 case SetupActivity::Load:return "Loading VOLTRA";
 default:return "";
 }
}
}
