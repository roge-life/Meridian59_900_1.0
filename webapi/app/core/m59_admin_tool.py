import asyncio
import logging
import sys

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    handlers=[logging.StreamHandler(sys.stdout)]
)
logger = logging.getLogger("m59-admin")

async def run_admin_command(host, port, command, timeout=2.0):
    """
    Carefully executes a command on the M59 maintenance port.
    Follows the pattern seen in working test scripts:
    1. Connect
    2. Optional brief wait/read to clear greeting
    3. Send command with \r
    4. Wait for response
    5. Send quit\r
    6. Close
    """
    reader, writer = None, None
    try:
        logger.info(f"Connecting to {host}:{port}...")
        reader, writer = await asyncio.wait_for(
            asyncio.open_connection(host, port), 
            timeout=timeout
        )

        # 1. Clear initial greeting if any
        logger.debug("Clearing greeting buffer...")
        try:
            greeting = await asyncio.wait_for(reader.read(4096), timeout=0.5)
            if greeting:
                logger.info(f"Received Greeting: {greeting.decode(errors='replace').strip()}")
        except asyncio.TimeoutError:
            logger.debug("No greeting received (timed out), proceeding...")

        # 2. Send the actual command
        logger.info(f"Sending command: '{command}'")
        writer.write(f"{command}\r".encode())
        await writer.drain()

        # 3. Read the response
        logger.debug("Waiting for response...")
        try:
            # We use a slightly longer timeout for the response
            response = await asyncio.wait_for(reader.read(8192), timeout=2.0)
            decoded_response = response.decode(errors='replace').strip()
            logger.info(f"Response Received: {decoded_response}")
            
            # 4. Graceful Quit
            logger.debug("Sending 'quit'...")
            writer.write(b"quit\r")
            await writer.drain()
            
            return decoded_response
        except asyncio.TimeoutError:
            logger.warning("Timed out waiting for command response.")
            return None

    except Exception as e:
        logger.error(f"Communication Failure: {e}")
        raise
    finally:
        if writer:
            writer.close()
            try:
                await writer.wait_closed()
            except:
                pass
        logger.info("Connection closed.")

if __name__ == "__main__":
    # Quick CLI test
    if len(sys.argv) < 3:
        print("Usage: python3 m59_admin_tool.py <host> <command>")
        sys.exit(1)
        
    target_host = sys.argv[1]
    cmd = sys.argv[2]
    
    try:
        asyncio.run(run_admin_command(target_host, 9998, cmd))
    except KeyboardInterrupt:
        pass
