/*
 * Copyright (C) Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "wait_ready.h"
#include "common_cli.h"

#include "animated_spinner.h"
#include <multipass/logging/log.h>  // for debugging
#include <multipass/exceptions/cmd_exceptions.h>
#include <multipass/cli/argparser.h>
#include <multipass/cli/formatter.h>
#include <multipass/timer.h>

namespace mp = multipass;
namespace cmd = multipass::cmd;
namespace mpl = multipass::logging;

mp::ReturnCode cmd::WaitReady::run(mp::ArgParser* parser)
{
    auto ret = parse_args(parser);
    if (ret != ParseCode::Ok)
    {
        return parser->returnCodeFrom(ret);
    }

    // If the user has specified a timeout, we will create a timer
    std::unique_ptr<mp::utils::Timer> timer;

    if (parser->isSet("timeout")){
        timer = cmd::make_timer(parser->value("timeout").toInt(),
                                nullptr,
                                cerr,
                                "Timed out waiting for Multipass daemon to be ready.");
        timer->start();
    }
    
    auto on_success = [this, &timer](WaitReadyReply& reply) {
        if (timer)
            timer->stop();
        return ReturnCode::Ok;
    };

    auto on_failure = [this](grpc::Status& status) {

        cerr << "In on_failure handler for " << name() << "\n";
        cerr << "Status Code: " << static_cast<int>(status.error_code()) << "\n";
        cerr << "Status Message: " << status.error_message() << "\n";

        if (status.error_code() == grpc::StatusCode::NOT_FOUND && 
            status.error_message() == "cannot connect to the multipass socket")
        {
            // This is the expected state for when the daemon is not yet ready
            // Sleep for a short duration and signal to retry
            std::this_thread::sleep_for(std::chrono::seconds(500));
            return ReturnCode::Retry;
            std::cerr << "Multipass daemon is not ready yet. Retrying...\n";
        }
        // For any other error, we will handle it as a standard failure
        return standard_failure_handler_for(name(), cerr, status);
    };

    request.set_verbosity_level(parser->verbosityLevel());
    
    ReturnCode return_code;
    return_code = dispatch(&RpcMethod::wait_ready, request, on_success, on_failure);
    cerr << "Return Code: " << static_cast<int>(return_code) << "\n";
    while ((return_code = dispatch(&RpcMethod::wait_ready, request, on_success, on_failure)) == 
            ReturnCode::Retry)
        ;

    return return_code;
}

std::string cmd::WaitReady::name() const
{
    return "wait-ready";
}

QString cmd::WaitReady::short_help() const
{
    return QStringLiteral("Wait for the Multipass daemon to be ready");
}

QString cmd::WaitReady::description() const
{
    return QStringLiteral(
        "Wait for the Multipass daemon to be ready.\n"
        "This command will block until the daemon is ready to accept requests.\n"
        "It can be used to ensure that the daemon is running before executing "
        "other commands.\n"
        "If a timeout is specified, it will wait for that duration before "
        "exiting with an error if the daemon is not ready.");
}

mp::ParseCode cmd::WaitReady::parse_args(mp::ArgParser* parser)
{
    
    // QCommandLineOption timeoutOption("timeout",
    //                                      "Specify a timeout in seconds for the command to wait for the Multipass daemon to be ready.",);
    // parser->addOptions({timeoutOption});
    
    
    // Taking add_timeout() from common_cli.h 
    // I believe this was initially used for instance related commands
    mp::cmd::add_timeout(parser);

    auto status = parser->commandParse(this);

    // Check if the command was parsed successfully
    if (status != ParseCode::Ok)
    {
        return status;
    }

    try
    {
        mp::cmd::parse_timeout(parser);
    }
    catch (const mp::ValidationException& e)
    {
        cerr << "error: " << e.what() << "\n";
        return ParseCode::CommandLineError;
    }

    // // For wait-ready, we expect no positional arguments, only the --timeout option.
    // if (!parser->positionalArguments().isEmpty())
    // {
    //     cerr << "This command does not take any positional arguments\n";
    //     return ParseCode::CommandLineError;
    // }

    // // Handle the --timeout option if provided
    // if (parser->isSet("timeout"))
    // {
    //     // Ensure that the timeout value is provided and is a valid integer
    //     bool ok = false;
    //     if (parser->value("timeout").isEmpty())
    //     {
    //         cerr << "The --timeout option requires a value\n";
    //         return ParseCode::CommandLineError;
    //     }

    //     // Check if the timeout value is a valid integer and greater than zero
    //     int timeout = parser->value("timeout").toInt(&ok);

    //     if (!ok || timeout <= 0)
    //     {
    //         cerr << "Invalid value for --timeout. Must be a non-negative integer greater than 0.\n";
    //         cerr << "If you want to wait indefinitely, do not use the --timeout option.\n";
    //         return ParseCode::CommandLineError;
    //     }
    //     timeout_sec = timeout;
    // }

    return status;
}
