/*
 * client.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include "client.hxx"


namespace page {

shell_client::shell_client() :
resource{nullptr},
client{nullptr},
shell{nullptr},
ping_timer{nullptr},
ping_serial{0},
unresponsive{0},
destroy_listener{0}
{

}


}
