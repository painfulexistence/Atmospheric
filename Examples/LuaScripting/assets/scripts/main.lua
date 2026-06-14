local player
local cube
local time = 0

-- =============================================================================
-- Example ScriptableComponent: Spinner
-- A component that rotates a GameObject around an axis
-- =============================================================================
Spinner = {}
Spinner.__index = Spinner

function Spinner:new()
    local self = setmetatable({}, Spinner)
    self.speed = 1.0
    self.axis = vec3.new(0, 1, 0)  -- Rotate around Y axis by default
    return self
end

function Spinner:init()
    print("[Spinner] Initialized on " .. tostring(self.gameObject))
end

function Spinner:update(dt)
    local go = self.gameObject
    local rot = go.rotation
    rot.y = rot.y + self.speed * dt
    go.rotation = rot
end

-- =============================================================================
-- Example ScriptableComponent: Bobber
-- A component that bobs a GameObject up and down
-- =============================================================================
Bobber = {}
Bobber.__index = Bobber

function Bobber:new()
    local self = setmetatable({}, Bobber)
    self.amplitude = 0.5
    self.frequency = 1.0
    self.time = 0
    self.baseY = 0
    return self
end

function Bobber:init()
    -- Store the initial Y position
    self.baseY = self.gameObject.position.y
    print("[Bobber] Initialized with baseY=" .. self.baseY)
end

function Bobber:update(dt)
    self.time = self.time + dt
    local pos = self.gameObject.position
    pos.y = self.baseY + math.sin(self.time * self.frequency * math.pi * 2) * self.amplitude
    self.gameObject.position = pos
end

-- =============================================================================
-- Example ScriptableComponent: CharacterController
-- A physics-based character with ground detection and collision handling
-- =============================================================================
CharacterController = {}
CharacterController.__index = CharacterController

function CharacterController:new()
    local self = setmetatable({}, CharacterController)
    self.speed = 5.0
    self.jumpForce = 8.0
    self.gravity = -20.0
    self.velocityY = 0
    self.isGrounded = false
    self.groundCheckDistance = 0.1
    return self
end

function CharacterController:init()
    print("[CharacterController] Initialized")
end

function CharacterController:update(dt)
    local go = self.gameObject
    local pos = go.position

    -- Ground detection via raycast
    local rayStart = vec3.new(pos.x, pos.y + 0.5, pos.z)
    local rayEnd = vec3.new(pos.x, pos.y - self.groundCheckDistance, pos.z)
    local hit = atmos.physics.raycast(rayStart, rayEnd)

    if hit then
        self.isGrounded = true
        self.velocityY = 0
        -- Snap to ground
        pos.y = hit.point.y
    else
        self.isGrounded = false
    end

    -- Horizontal movement
    local move = vec3.new(0, 0, 0)
    if atmos.input.isKeyDown(atmos.keys.W) then move.z = 1 end
    if atmos.input.isKeyDown(atmos.keys.S) then move.z = -1 end
    if atmos.input.isKeyDown(atmos.keys.A) then move.x = -1 end
    if atmos.input.isKeyDown(atmos.keys.D) then move.x = 1 end

    -- Normalize horizontal movement
    local len = math.sqrt(move.x * move.x + move.z * move.z)
    if len > 0 then
        move.x = move.x / len * self.speed
        move.z = move.z / len * self.speed
    end

    -- Jump
    if self.isGrounded and atmos.input.isKeyPressed(atmos.keys.SPACE) then
        self.velocityY = self.jumpForce
        print("[CharacterController] Jump!")
    end

    -- Apply gravity
    if not self.isGrounded then
        self.velocityY = self.velocityY + self.gravity * dt
    end

    -- Apply movement
    pos.x = pos.x + move.x * dt
    pos.y = pos.y + self.velocityY * dt
    pos.z = pos.z + move.z * dt

    go.position = pos
end

function CharacterController:onCollision(other)
    print("[CharacterController] Collided with: " .. tostring(other))
end

-- =============================================================================
-- Example ScriptableComponent: Collectible
-- Shows collision callback usage
-- =============================================================================
Collectible = {}
Collectible.__index = Collectible

function Collectible:new()
    local self = setmetatable({}, Collectible)
    self.collected = false
    return self
end

function Collectible:init()
    print("[Collectible] Spawned")
end

function Collectible:onCollision(other)
    if not self.collected and other.name == "Player" then
        self.collected = true
        print("[Collectible] Picked up by player!")
        -- Disable the object
        self.gameObject.isActive = false
    end
end

-- =============================================================================
-- Game Code
-- =============================================================================

function load()
    print("Lua: load() called")

    -- -- Create Camera
    -- local camObj = atmos.world.spawn(0, 5, 10)
    -- camObj.name = "MainCamera"
    -- camObj:addCamera({
    --     fov = 60.0,
    --     aspect = 16.0/9.0,
    --     near = 0.1,
    --     far = 100.0
    -- })

    -- Create Directional Light
    local lightObj = atmos.world.spawn(0, 10, 0)
    lightObj.name = "Sun"
    lightObj.rotation = vec3.new(math.rad(45), math.rad(45), 0) -- Angled down
    lightObj:addLight({
        type = 0, -- Directional
        intensity = 1.0,
        castShadow = true
    })

    -- Create the cube mesh
    local cubeMesh = atmos.assets.createCubeMesh("Cube", 1.0)

    -- Create a simple cube with ScriptableComponents
    cube = atmos.world.spawn(0, 0, 0)
    if cube then
        cube.name = "MyCube"
        cube.position = vec3.new(5, 0, 0)
        cube.rotation = vec3.new(0, 0, math.rad(45))
        
        cube:addMesh("Cube")
        cube:addScript("Spinner")
        cube:addScript("Bobber")

        local spinner = cube:getScript()
        if spinner then
            spinner.instance.speed = 5.0  -- Faster rotation
        end
    end

    -- Create player entity
    player = atmos.world.spawn(0, 1, -5)
    player.name = "Player"

    -- Web Audio test sound
    testSound = atmos.audio.loadSound("assets/target.ogg")
    print("[Lua] Loaded test sound ID: " .. tostring(testSound))

    print("[Lua] Game loaded!")
    print("[Lua] Press WASD to move, ESC to quit")
    print("[Lua] The cube will rotate and bob using ScriptableComponents!")
end

function update(dt)
    time = time + dt

    if player then
        local speed = 5.0
        local pos = player.position
        local moved = false

        if atmos.input.isKeyDown(atmos.keys.W) then
            pos.z = pos.z + speed * dt
            moved = true
        end
        if atmos.input.isKeyDown(atmos.keys.S) then
            pos.z = pos.z - speed * dt
            moved = true
        end
        if atmos.input.isKeyDown(atmos.keys.A) then
            pos.x = pos.x - speed * dt
            moved = true
        end
        if atmos.input.isKeyDown(atmos.keys.D) then
            pos.x = pos.x + speed * dt
            moved = true
        end

        if moved then
            player.position = pos
        end
    end

    -- Quit on ESC
    if atmos.input.isKeyDown(atmos.keys.ESCAPE) then
        print("[Lua] Quitting...")
        atmos.quit()
    end

    -- Reload shaders on R
    if atmos.input.isKeyPressed(atmos.keys.R) then
        print("[Lua] Reloading shaders...")
        atmos.graphics.reloadShaders()
    end

    -- Play sound on SPACE
    if atmos.input.isKeyPressed(atmos.keys.SPACE) then
        print("[Lua] Playing test sound (Web Audio)...")
        atmos.audio.playSoundVariation(testSound, 0.15, 0.1)
    end
end

function draw()
    -- Optional: Custom draw logic
    -- For now, rendering is handled by the C++ engine
end
