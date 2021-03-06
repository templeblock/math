/*
Copyright (C) 2017-2019 Topological Manifold

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#if 1

#include "com/error.h"
#include "init/init.h"
#include "ui/application.h"

#include <exception>

int main(int argc, char* argv[])
{
        try
        {
                try
                {
                        Initialization init;

                        return application(argc, argv);
                }
                catch (std::exception& e)
                {
                        error_fatal(std::string("Error in the main function\n") + e.what());
                }
                catch (...)
                {
                        error_fatal("Unknown error in the main function");
                }
        }
        catch (...)
        {
                error_fatal("Exception in the main function exception handlers");
        }
}

#else

#include "com/log.h"
#include "com/math.h"
#include "com/print.h"
#include "init/init.h"

int main()
{
        Initialization init;
}

#endif
